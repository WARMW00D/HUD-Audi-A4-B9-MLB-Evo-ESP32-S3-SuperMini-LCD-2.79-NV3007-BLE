/*
  hud_ble_cams.cpp — загрузка базы камер (cams.bin) с телефона по BLE.
  Тот же протокол v1, что и у OTA, но данные идут потоком (Write Without Response)
  окнами по 8 пакетов; телефон после окна читает статус и при расхождении смещения
  повторяет с received (go-back-N), поэтому потерянный пакет не рвёт загрузку.
  Остальное как у OTA (hud_ota_core.h): START(размер+SHA-256) -> пакеты
  offset(4)+данные -> END (проверка SHA-256) -> COMMIT (запись первой страницы с
  сигнатурой, перечитывание базы из флеши в PSRAM, повторная проверка SHA-256).
  Перезагрузка HUD не нужна. Первая страница (с "CAM1") пишется последней: оборванная
  загрузка оставляет «пустую» флешь, а не наполовину записанную базу.
*/
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#include "freertos/semphr.h"
#include "hud_ble_cams.h"
#include "hud_ota_core.h"
#include "hud_cams.h"
#include "hud_config.h"

#if HUD_CAMS
namespace camup {
static const char *CAM_SERVICE="74d0a200-3d92-4f50-9b1a-478142000011";
static const char *CAM_CONTROL="74d0a200-3d92-4f50-9b1a-478142000012";
static const char *CAM_DATA="74d0a200-3d92-4f50-9b1a-478142000013";

class CamStorage : public HudOtaStorage {
    mbedtls_sha256_context sha={};
    uint8_t page[256], hold0[256], digest[32];
    uint32_t pos=0, total=0;
    bool hashing=false, active=false;

    bool sha_update(const uint8_t *d,size_t n) {
#if MBEDTLS_VERSION_MAJOR >= 3
        return mbedtls_sha256_update(&sha,d,n)==0;
#else
        return mbedtls_sha256_update_ret(&sha,d,n)==0;
#endif
    }
    /* Страница [base, base+256): первая держится в RAM до COMMIT */
    bool flush(uint32_t base) {
        if (base==0) { memcpy(hold0,page,256); }
        else {
            if (!(base&4095) && !hud_cams_flash_erase(base)) return false;
            if (!hud_cams_flash_write(base,page,256)) return false;
        }
        memset(page,0xFF,sizeof page);
        return true;
    }
public:
    uint32_t capacity() const override { return hud_cams_max_db_bytes(); }
    bool begin(uint32_t size) override {
        abort();
        if (!size || size>capacity()) return false;
        memset(page,0xFF,sizeof page); memset(hold0,0xFF,sizeof hold0);
        if (!hud_cams_flash_erase(0)) return false;           // старая сигнатура исчезает сразу
        mbedtls_sha256_init(&sha); hashing=true;
#if MBEDTLS_VERSION_MAJOR >= 3
        const int r=mbedtls_sha256_starts(&sha,0);
#else
        const int r=mbedtls_sha256_starts_ret(&sha,0);
#endif
        if (r) { abort(); return false; }
        pos=0; total=size; active=true;
        return true;
    }
    bool write(const uint8_t *d,size_t n) override {
        if (!active || !hashing || !sha_update(d,n)) return false;
        while (n) {
            const uint32_t in_page=pos&255;
            size_t k=256-in_page; if (k>n) k=n;
            memcpy(page+in_page,d,k);
            pos+=k; d+=k; n-=k;
            if (!(pos&255) && !flush(pos-256)) return false;
        }
        return true;
    }
    HudOtaError finish(const uint8_t expected[32]) override {
        if (!active || !hashing || pos!=total) return HudOtaError::State;
        if ((pos&255) && !flush(pos&~255u)) return HudOtaError::Storage;
        uint8_t actual[32];
#if MBEDTLS_VERSION_MAJOR >= 3
        const int r=mbedtls_sha256_finish(&sha,actual);
#else
        const int r=mbedtls_sha256_finish_ret(&sha,actual);
#endif
        mbedtls_sha256_free(&sha); hashing=false;
        if (r) return HudOtaError::Storage;
        unsigned diff=0; for (unsigned i=0;i<32;++i) diff|=actual[i]^expected[i];
        if (diff) return HudOtaError::Hash;
        memcpy(digest,expected,32);
        return HudOtaError::None;
    }
    bool commit() override {
        if (!active || hashing) return false;
        if (!hud_cams_flash_write(0,hold0,256)) return false;   // сигнатура и заголовок — последними
        return hud_cams_reload(digest,total);
    }
    void abort() override {
        if (hashing) mbedtls_sha256_free(&sha);
        hashing=active=false; pos=total=0;
    }
};

static CamStorage storage;
static HudOtaCore core(storage,false);
static SemaphoreHandle_t mutex;
static NimBLEServer *server_;
static NimBLECharacteristic *control_char;
static HudCamsAuthorize authorize;
static uint16_t session_handle=BLE_HS_CONN_HANDLE_NONE;
static uint32_t last_activity;
static bool fast_link;

struct Lock { Lock(){xSemaphoreTake(mutex,portMAX_DELAY);} ~Lock(){xSemaphoreGive(mutex);} };

static void sync_link_speed(uint8_t state) {
    if (!server_ || session_handle==BLE_HS_CONN_HANDLE_NONE) return;
    const bool want_fast = state==uint8_t(HudOtaState::Receiving) || state==uint8_t(HudOtaState::Verified);
    if (want_fast==fast_link) return;
    server_->updateConnParams(session_handle, want_fast?6:24, want_fast?12:48, 0, 400);
    fast_link=want_fast;
}
static bool cam_allowed(NimBLEConnInfo &ci) {
    return ci.isEncrypted() && ci.isAuthenticated() && ci.isBonded() && authorize(ci.getConnHandle());
}
static void publish() {
    uint8_t status[16]; core.status(status); control_char->setValue(status,sizeof status);
    sync_link_speed(status[1]);
}
class ControlCallbacks : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic *,NimBLEConnInfo &ci) override {
        if (!cam_allowed(ci)) { server_->disconnect(ci); return; }
        Lock lock; publish();
    }
    void onWrite(NimBLECharacteristic *,NimBLEConnInfo &ci) override {
        if (!cam_allowed(ci)) { server_->disconnect(ci); return; }
        Lock lock;
        if (session_handle!=BLE_HS_CONN_HANDLE_NONE && session_handle!=ci.getConnHandle()) return;
        const auto value=control_char->getValue();
        session_handle=ci.getConnHandle(); last_activity=millis();
        core.control(value.data(),value.size()); publish();
        if (core.state()==HudOtaState::Committed) Serial.println("[cam] upload committed, database active");
    }
};
class DataCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c,NimBLEConnInfo &ci) override {
        if (!cam_allowed(ci)) { server_->disconnect(ci); return; }
        Lock lock;
        if (session_handle!=ci.getConnHandle()) return;
        const auto value=c->getValue(); last_activity=millis();
        core.packet(value.data(),value.size()); publish();
    }
};
static ControlCallbacks control_callbacks;
static DataCallbacks data_callbacks;

void begin(NimBLEServer *server,HudCamsAuthorize check) {
    server_=server; authorize=check; mutex=xSemaphoreCreateMutex();
    if (!mutex) abort();
    auto *service=server->createService(CAM_SERVICE);
    control_char=service->createCharacteristic(CAM_CONTROL,NIMBLE_PROPERTY::READ|NIMBLE_PROPERTY::WRITE|
        NIMBLE_PROPERTY::READ_AUTHEN|NIMBLE_PROPERTY::WRITE_AUTHEN,64);
    auto *data=service->createCharacteristic(CAM_DATA,NIMBLE_PROPERTY::WRITE|NIMBLE_PROPERTY::WRITE_NR|NIMBLE_PROPERTY::WRITE_AUTHEN,244);
    control_char->setCallbacks(&control_callbacks); data->setCallbacks(&data_callbacks);
    publish(); service->start();
    Serial.println("[cam] BLE upload service ready");
}
void disconnect(uint16_t handle) {
    if (!mutex) return;
    Lock lock;
    if (session_handle==handle) {
        core.cancel();
        fast_link=false;
        session_handle=BLE_HS_CONN_HANDLE_NONE; publish();
    }
}
bool busy() {
    if (!mutex) return false;
    Lock lock; return core.state()==HudOtaState::Receiving || core.state()==HudOtaState::Verified;
}
void poll() {
    if (!mutex) return;
    Lock lock;
    const bool receiving=core.state()==HudOtaState::Receiving || core.state()==HudOtaState::Verified;
    if (receiving && (uint32_t)(millis()-last_activity)>120000) {
        core.timeout(); publish(); Serial.println("[cam] Transfer timed out; database not changed.");
    }
}
} // namespace camup
void hud_ble_cams_begin(NimBLEServer *server,HudCamsAuthorize check) { camup::begin(server,check); }
void hud_ble_cams_disconnect(uint16_t handle) { camup::disconnect(handle); }
bool hud_ble_cams_busy() { return camup::busy(); }
void hud_ble_cams_poll() { camup::poll(); }
#else
void hud_ble_cams_begin(NimBLEServer *,HudCamsAuthorize) {}
void hud_ble_cams_disconnect(uint16_t) {}
void hud_ble_cams_poll() {}
bool hud_ble_cams_busy() { return false; }
#endif
