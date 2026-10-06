#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Protocol v1. All integer fields are little-endian. No dynamic allocation.
enum class HudOtaState : uint8_t { Idle=0, Receiving=1, Verified=2, Committed=3, Error=4 };
enum class HudOtaError : uint8_t {
    None=0, Protocol=1, NoPartition=2, Size=3, Storage=4, Offset=5,
    Hash=6, Image=7, State=8, Timeout=9
};
class HudOtaStorage {
public:
    virtual ~HudOtaStorage() = default;
    virtual uint32_t capacity() const = 0;
    virtual bool begin(uint32_t size) = 0;
    virtual bool write(const uint8_t *data, size_t size) = 0;
    virtual HudOtaError finish(const uint8_t expected_sha[32]) = 0;
    virtual bool commit() = 0;
    virtual void abort() = 0;
};
class HudOtaCore {
    HudOtaStorage &storage;
    HudOtaState state_ = HudOtaState::Idle;
    HudOtaError error_ = HudOtaError::None;
    uint32_t total_ = 0, received_ = 0;
    uint8_t digest_[32] = {};
    void fail(HudOtaError error) {
        storage.abort(); state_=HudOtaState::Error; error_=error;
    }
public:
    explicit HudOtaCore(HudOtaStorage &backend) : storage(backend) {}
    static uint32_t get32(const uint8_t *p) {
        return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
    }
    static void put32(uint8_t *p, uint32_t n) {
        for (unsigned i=0; i<4; ++i) p[i]=uint8_t(n>>(8*i));
    }
    HudOtaState state() const { return state_; }
    bool busy() const { return state_==HudOtaState::Receiving || state_==HudOtaState::Verified || state_==HudOtaState::Committed; }
    void status(uint8_t out[16]) const {
        out[0]=1; out[1]=uint8_t(state_); out[2]=uint8_t(error_); out[3]=0;
        put32(out+4,received_); put32(out+8,total_); put32(out+12,storage.capacity());
    }
    void cancel() {
        if (state_==HudOtaState::Committed) return; // Selection is already durable.
        storage.abort(); state_=HudOtaState::Idle; error_=HudOtaError::None;
        received_=total_=0; memset(digest_,0,sizeof(digest_));
    }
    void timeout() { if (state_==HudOtaState::Receiving || state_==HudOtaState::Verified) fail(HudOtaError::Timeout); }
    void control(const uint8_t *data, size_t size) {
        if (state_==HudOtaState::Committed) return; // Duplicate commit/late cancel cannot undo it.
        if (!data || !size) { fail(HudOtaError::Protocol); return; }
        if (data[0]==1) { // START: command + size + SHA-256 (37 bytes)
            if (size!=37) { fail(HudOtaError::Protocol); return; }
            cancel(); total_=get32(data+1); memcpy(digest_,data+5,32);
            const uint32_t capacity=storage.capacity();
            if (!capacity) { fail(HudOtaError::NoPartition); return; }
            if (total_<36 || total_>capacity) { fail(HudOtaError::Size); return; }
            if (!storage.begin(total_)) { fail(HudOtaError::Storage); return; }
            state_=HudOtaState::Receiving;
        } else if (size!=1) fail(HudOtaError::Protocol);
        else if (data[0]==4) cancel();
        else if (data[0]==2) { // END: validate, but do not change boot selection.
            if (state_!=HudOtaState::Receiving) { fail(HudOtaError::State); return; }
            if (received_!=total_) { fail(HudOtaError::Size); return; }
            const auto result=storage.finish(digest_);
            if (result!=HudOtaError::None) { fail(result); return; }
            state_=HudOtaState::Verified;
        } else if (data[0]==3) { // COMMIT: explicit boot selection after END confirmation.
            if (state_!=HudOtaState::Verified) { fail(HudOtaError::State); return; }
            if (!storage.commit()) { fail(HudOtaError::Storage); return; }
            state_=HudOtaState::Committed;
        } else fail(HudOtaError::Protocol);
    }
    void packet(const uint8_t *data, size_t size) {
        if (state_==HudOtaState::Committed) return;
        if (state_!=HudOtaState::Receiving) { fail(HudOtaError::State); return; }
        if (!data || size<=4 || size>244) { fail(HudOtaError::Protocol); return; }
        if (get32(data)!=received_) { fail(HudOtaError::Offset); return; }
        const size_t n=size-4;
        if (n>total_-received_) { fail(HudOtaError::Size); return; }
        if (received_==0) {
            // Raw application image, ESP32-S3 chip ID 9, application descriptor.
            // A merged image, another chip or a bootloader is rejected before writing.
            const uint8_t *p=data+4;
            if (n<36 || p[0]!=0xe9 || p[12]!=9 || p[13]!=0 || get32(p+32)!=0xabcd5432) {
                fail(HudOtaError::Image); return;
            }
        }
        if (!storage.write(data+4,n)) { fail(HudOtaError::Storage); return; }
        received_+=uint32_t(n);
    }
};
