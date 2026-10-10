#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#include "freertos/semphr.h"
#include "hud_ble_ota.h"
#include "hud_ota_core.h"
#include "hud_mockup.h"

static const char *OTA_SERVICE="74d0a200-3d92-4f50-9b1a-478142000001";
static const char *OTA_CONTROL="74d0a200-3d92-4f50-9b1a-478142000002";
static const char *OTA_DATA="74d0a200-3d92-4f50-9b1a-478142000003";

class EspOtaStorage : public HudOtaStorage {
    const esp_partition_t *target=nullptr;
    esp_ota_handle_t handle=0;
    mbedtls_sha256_context sha={};
    bool opened=false, hashing=false;
public:
    uint32_t capacity() const override {
        if (!esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_OTA,nullptr) ||
            !esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_0,nullptr) ||
            !esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_1,nullptr)) return 0;
        const auto *next=esp_ota_get_next_update_partition(nullptr);
        const auto *running=esp_ota_get_running_partition();
        if (!next || !running || next->address==running->address) return 0;
        return next->size;
    }
    bool begin(uint32_t size) override {
        abort();
        if (!capacity() || size>capacity()) return false;
        target=esp_ota_get_next_update_partition(nullptr);
        // Incremental erase avoids erasing a whole slot in a BLE callback.
        if (esp_ota_begin(target,OTA_WITH_SEQUENTIAL_WRITES,&handle)!=ESP_OK) { target=nullptr; return false; }
        opened=true; mbedtls_sha256_init(&sha); hashing=true;
#if MBEDTLS_VERSION_MAJOR >= 3
        const int result=mbedtls_sha256_starts(&sha,0);
#else
        const int result=mbedtls_sha256_starts_ret(&sha,0);
#endif
        if (result) { abort(); return false; }
        return true;
    }
    bool write(const uint8_t *data,size_t size) override {
        if (!opened || !hashing || esp_ota_write(handle,data,size)!=ESP_OK) return false;
#if MBEDTLS_VERSION_MAJOR >= 3
        return mbedtls_sha256_update(&sha,data,size)==0;
#else
        return mbedtls_sha256_update_ret(&sha,data,size)==0;
#endif
    }
    HudOtaError finish(const uint8_t expected[32]) override {
        if (!opened || !hashing) return HudOtaError::State;
        uint8_t actual[32];
#if MBEDTLS_VERSION_MAJOR >= 3
        const int result=mbedtls_sha256_finish(&sha,actual);
#else
        const int result=mbedtls_sha256_finish_ret(&sha,actual);
#endif
        mbedtls_sha256_free(&sha); hashing=false;
        if (result) return HudOtaError::Storage;
        unsigned difference=0; for(unsigned i=0;i<32;++i) difference|=actual[i]^expected[i];
        if (difference) return HudOtaError::Hash;
        // esp_ota_end always releases the handle, including validation failure.
        opened=false;
        if (esp_ota_end(handle)!=ESP_OK) return HudOtaError::Image;
        return HudOtaError::None;
    }
    bool commit() override { return target && !opened && esp_ota_set_boot_partition(target)==ESP_OK; }
    void abort() override {
        if (opened) esp_ota_abort(handle);
        if (hashing) mbedtls_sha256_free(&sha);
        opened=hashing=false; target=nullptr;
    }
};
static EspOtaStorage storage;
static HudOtaCore core(storage);
static SemaphoreHandle_t ota_mutex;
static NimBLEServer *ota_server;
static NimBLECharacteristic *control_char;
static HudOtaAuthorize authorize;
static uint16_t session_handle=BLE_HS_CONN_HANDLE_NONE;
static uint32_t last_activity, committed_since;
static bool restart_pending;
static bool fast_link;

static void sync_link_speed(uint8_t state) {
    if (!ota_server || session_handle == BLE_HS_CONN_HANDLE_NONE) return;
    const bool want_fast = state == uint8_t(HudOtaState::Receiving) ||
                           state == uint8_t(HudOtaState::Verified);
    if (want_fast == fast_link) return;
    /* BLE intervals are in 1.25 ms units: 7.5..15 ms for OTA,
       30..60 ms for normal operation. */
    ota_server->updateConnParams(session_handle, want_fast ? 6 : 24,
                                 want_fast ? 12 : 48, 0, 400);
    fast_link = want_fast;
    Serial.printf("[ota] BLE connection interval: %s\n", want_fast ? "fast" : "normal");
}
struct OtaLock {
    OtaLock(){xSemaphoreTake(ota_mutex,portMAX_DELAY);}
    ~OtaLock(){xSemaphoreGive(ota_mutex);}
};
static bool allowed(NimBLEConnInfo &ci) {
    // Check before taking ota_mutex: never reverse the settings -> OTA lock order.
    return ci.isEncrypted() && ci.isAuthenticated() && ci.isBonded() && authorize(ci.getConnHandle());
}
static void publish() {
    uint8_t status[16]; core.status(status); control_char->setValue(status,sizeof(status));
    hud_ota_ui_status(status[1], HudOtaCore::get32(status + 4), HudOtaCore::get32(status + 8));
    sync_link_speed(status[1]);
}
class ControlCallbacks : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic *,NimBLEConnInfo &ci) override {
        if (!allowed(ci)) { ota_server->disconnect(ci); return; }
        OtaLock lock; publish();
    }
    void onWrite(NimBLECharacteristic *,NimBLEConnInfo &ci) override {
        if (!allowed(ci)) { ota_server->disconnect(ci); return; }
        OtaLock lock;
        if (session_handle!=BLE_HS_CONN_HANDLE_NONE && session_handle!=ci.getConnHandle()) return;
        const auto value=control_char->getValue();
        session_handle=ci.getConnHandle(); last_activity=millis();
        core.control(value.data(),value.size()); publish();
        if (core.state()==HudOtaState::Committed && !restart_pending) {
            restart_pending=true; committed_since=millis();
            Serial.println("[ota] Image verified; boot partition selected. Restart scheduled.");
        }
    }
};
class DataCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c,NimBLEConnInfo &ci) override {
        if (!allowed(ci)) { ota_server->disconnect(ci); return; }
        OtaLock lock;
        if (session_handle!=ci.getConnHandle()) return;
        const auto value=c->getValue(); last_activity=millis();
        core.packet(value.data(),value.size()); publish();
    }
};
static ControlCallbacks control_callbacks;
static DataCallbacks data_callbacks;
void hud_ble_ota_begin(NimBLEServer *server,HudOtaAuthorize check) {
    ota_server=server; authorize=check; ota_mutex=xSemaphoreCreateMutex();
    if (!ota_mutex) abort();
    auto *service=server->createService(OTA_SERVICE);
    control_char=service->createCharacteristic(OTA_CONTROL,NIMBLE_PROPERTY::READ|NIMBLE_PROPERTY::WRITE|
        NIMBLE_PROPERTY::READ_AUTHEN|NIMBLE_PROPERTY::WRITE_AUTHEN,64);
    auto *data=service->createCharacteristic(OTA_DATA,NIMBLE_PROPERTY::WRITE|NIMBLE_PROPERTY::WRITE_AUTHEN,244);
    control_char->setCallbacks(&control_callbacks); data->setCallbacks(&data_callbacks);
    publish(); service->start();
    Serial.printf("[ota] BLE service ready, next slot capacity=%lu\n",(unsigned long)storage.capacity());
}
void hud_ble_ota_disconnect(uint16_t handle) {
    if (!ota_mutex) return;
    OtaLock lock;
    if (session_handle==handle) {
        core.cancel();
        if (fast_link) {
            ota_server->updateConnParams(handle, 24, 48, 0, 400);
            fast_link = false;
        }
        session_handle=BLE_HS_CONN_HANDLE_NONE; publish();
        // A committed image must still reboot if Android misses the final reply.
    }
}
bool hud_ble_ota_busy() {
    if (!ota_mutex) return false;
    OtaLock lock; return core.busy();
}
void hud_ble_ota_poll() {
    bool restart=false;
    {
        OtaLock lock;
        if (restart_pending && (uint32_t)(millis()-committed_since)>=2000) restart=true;
        else if (core.busy() && !restart_pending && (uint32_t)(millis()-last_activity)>120000) {
            core.timeout(); publish(); Serial.println("[ota] Transfer timed out; current firmware retained.");
        }
    }
    if (restart) ESP.restart();
}
