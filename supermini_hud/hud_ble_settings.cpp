#include <Arduino.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include "freertos/semphr.h"
#include "hud_config.h"
#include "user_config.h"
#include "hud_mockup.h"
#include "hud_ble_settings.h"
#include "hud_ble_ota.h"
#include <cstring>
#include <cctype>

static_assert(HUD_SETTINGS_PIN >= 100000 && HUD_SETTINGS_PIN <= 999999,
              "HUD_SETTINGS_PIN must contain six digits.");
#if CONFIG_BT_NIMBLE_EXT_ADV
#error "HUD settings use legacy directed advertising: disable extended advertising."
#endif
static const char *SERVICE = "74d0a100-3d92-4f50-9b1a-478142000001";
static const char *UUIDS[5] = {
    "74d0a100-3d92-4f50-9b1a-478142000002", /* PSD */
    "74d0a100-3d92-4f50-9b1a-478142000003", /* VZE */
    "74d0a100-3d92-4f50-9b1a-478142000004", /* RU / EN */
    "74d0a100-3d92-4f50-9b1a-478142000005", /* km / miles */
    "74d0a100-3d92-4f50-9b1a-478142000006"  /* litres / US gallons */
};
struct Stored {
    uint8_t version;
    uint8_t value[5];
    uint8_t owned;
    uint8_t type;
    char address[18];
};
static Stored state;
static Preferences prefs;
static SemaphoreHandle_t mutex;
static NimBLEServer *server;
static NimBLECharacteristic *chars[5];
static bool gateway_security;
static uint16_t phone_handle = BLE_HS_CONN_HANDLE_NONE;
static uint32_t phone_since;
static bool phone_authenticated;
static bool reset_pending;

struct Lock {
    Lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(mutex); }
};
static NimBLEAddress owner() { return NimBLEAddress(std::string(state.address), state.type); }
static bool same_owner(const NimBLEConnInfo &ci) {
    return state.owned && ci.getIdAddress().toString() == state.address &&
           (ci.getIdAddress().getType() & 1) == state.type;
}
static bool authorized(const NimBLEConnInfo &ci) {
    return !reset_pending && !gateway_security && phone_authenticated &&
           phone_handle == ci.getConnHandle() && same_owner(ci) && ci.isBonded() &&
           ci.isEncrypted() && ci.isAuthenticated();
}
static bool ota_authorized_handle(uint16_t handle) {
    Lock lock;
    return !reset_pending && !gateway_security && phone_authenticated && state.owned && phone_handle==handle;
}
static bool save(const Stored &next) {
    return prefs.putBytes("state", &next, sizeof(next)) == sizeof(next);
}
static void apply() {
    hud_set_psd(state.value[0]); hud_set_vze(state.value[1]);
    hud_set_lang(state.value[2]); hud_set_units(state.value[3]);
    hud_set_gallons(state.value[4]);
}
/* Call under mutex, never automatically advertise from disconnect callbacks. */
static void advertise() {
    if (reset_pending || gateway_security || phone_handle != BLE_HS_CONN_HANDLE_NONE) return;
    auto *adv = NimBLEDevice::getAdvertising();
    if (adv->isAdvertising()) return;
    adv->reset();
    adv->enableScanResponse(false);
    if (state.owned) {
        NimBLEAddress peer = owner();
        /* Anonymous undirected advertising permits Android reconnecting.
           Keep controller filtering; check the resolved identity after security. */
        if (!NimBLEDevice::onWhiteList(peer) && !NimBLEDevice::whiteListAdd(peer)) {
            Serial.println("[settings] whitelist failed; staying closed."); return;
        }
        adv->setScanFilter(true, true);
        adv->setDiscoverableMode(BLE_GAP_DISC_MODE_NON);
        adv->setConnectableMode(BLE_GAP_CONN_MODE_UND);
        if (!adv->start()) Serial.println("[settings] Hidden advertising start failed.");
    } else {
        adv->setScanFilter(false, false);
        adv->setDiscoverableMode(BLE_GAP_DISC_MODE_GEN);
        adv->setConnectableMode(BLE_GAP_CONN_MODE_UND);
        adv->setName(HUD_SETTINGS_BLE_NAME);
        adv->addServiceUUID(SERVICE);
        adv->start();
    }
}
class ServerCallbacks : public NimBLEServerCallbacks {
    uint32_t onPassKeyDisplay() override { return HUD_SETTINGS_PIN; }
    void onConnect(NimBLEServer *s, NimBLEConnInfo &ci) override {
        bool reject;
        {
            Lock lock;
            /* A phone's RPA/identity may not yet be resolved here. The controller
               whitelist filters incoming links; verify owner after encryption. */
            reject = reset_pending || gateway_security || phone_handle != BLE_HS_CONN_HANDLE_NONE;
            if (!reject) {
                phone_handle = ci.getConnHandle(); phone_since = millis();
                phone_authenticated = false;
            }
        }
        NimBLEDevice::getAdvertising()->stop();
        if (reject) { s->disconnect(ci); return; }
        Serial.println("[settings] Link connected; checking stored keys.");
        if (!NimBLEDevice::startSecurity(ci.getConnHandle())) {
            Serial.println("[settings] Could not start security."); s->disconnect(ci);
        }
    }
    void onDisconnect(NimBLEServer *, NimBLEConnInfo &ci, int) override {
        hud_ble_ota_disconnect(ci.getConnHandle());
        Lock lock;
        if (phone_handle == ci.getConnHandle()) {
            phone_handle = BLE_HS_CONN_HANDLE_NONE; phone_authenticated = false;
        }
    }
    void onAuthenticationComplete(NimBLEConnInfo &ci) override {
        bool reject = false;
        {
            Lock lock;
            if (reset_pending || gateway_security || phone_handle != ci.getConnHandle() ||
                !ci.isEncrypted() || !ci.isAuthenticated() || !ci.isBonded()) reject = true;
            else if (state.owned) reject = !same_owner(ci);
            else {
                Stored next = state;
                const auto id = ci.getIdAddress();
                const auto addr = id.toString();
                if (addr.size() != 17) reject = true;
                else {
                    next.owned = 1; next.type = id.getType() & 1;
                    memcpy(next.address, addr.c_str(), 18);
                    if (!save(next)) reject = true;
                    else { state = next; Serial.println("[settings] Phone saved. Hidden filtered mode."); }
                }
            }
            if (!reject) {
                phone_authenticated = true;
                Serial.println("[settings] Owner authenticated; settings access ready.");
            }
        }
        if (reject) {
            /* Never erase an existing owner's bond on failed authentication. */
            Serial.printf("[settings] Auth rejected: encrypted=%d authenticated=%d bonded=%d id=%s type=%u\n",
                ci.isEncrypted(), ci.isAuthenticated(), ci.isBonded(), ci.getIdAddress().toString().c_str(),
                (unsigned)ci.getIdAddress().getType());
            server->disconnect(ci);
        }
    }
};
static ServerCallbacks server_callbacks;
class SettingCallbacks : public NimBLECharacteristicCallbacks {
    const unsigned index;
public:
    explicit SettingCallbacks(unsigned i) : index(i) {}
    void onRead(NimBLECharacteristic *c, NimBLEConnInfo &ci) override {
        bool ok;
        { Lock lock; ok = authorized(ci); c->setValue(&state.value[index], ok ? 1 : 0); }
        if (!ok) server->disconnect(ci);
    }
    void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &ci) override {
        bool ok;
        {
            Lock lock;
            ok = authorized(ci);
            if (ok) {
                const auto v = c->getValue();
                if (v.size() == 1 && v.data()[0] <= 1) {
                    Stored next = state; next.value[index] = v.data()[0];
                    if (next.value[index] == state.value[index] || save(next)) { state = next; apply(); }
                    else Serial.println("[settings] NVS write failed; value unchanged.");
                } else Serial.println("[settings] Expected one byte: 00 or 01.");
                c->setValue(&state.value[index], 1); /* readback is authoritative */
            }
        }
        if (!ok) server->disconnect(ci);
    }
};
static SettingCallbacks setting_callbacks[5] = {
    SettingCallbacks(0), SettingCallbacks(1), SettingCallbacks(2), SettingCallbacks(3), SettingCallbacks(4)
};
void hud_ble_settings_begin() {
    mutex = xSemaphoreCreateMutex();
    if (!mutex || !prefs.begin("hud-settings", false)) abort();
    state = {1, {HUD_PSD_LIMITS, HUD_VZE_SIGNS, HUD_LANG, HUD_UNITS, HUD_VOLUME_GAL}, 0, 0, {0}};
    const size_t stored_length = prefs.getBytesLength("state");
    if (stored_length && stored_length != sizeof(Stored)) {
        Serial.println("[settings] Invalid NVS record size; refusing to open pairing."); abort();
    }
    if (stored_length == sizeof(Stored)) {
        Stored saved;
        prefs.getBytes("state", &saved, sizeof(saved));
        bool valid = saved.version == 1 && saved.owned <= 1;
        for (auto v : saved.value) valid = valid && v <= 1;
        if (saved.owned) {
            valid = valid && saved.address[17] == '\0' && saved.type <= 1;
            for (unsigned i = 0; i < 17; ++i)
                valid = valid && ((i % 3 == 2) ? saved.address[i] == ':' : isxdigit((unsigned char)saved.address[i]));
        }
        if (!valid) { Serial.println("[settings] Invalid saved state; refusing to open pairing."); abort(); }
        state = saved;
    }
    apply();
    pinMode(HUD_SETTINGS_BOOT_PIN, INPUT_PULLUP);
    NimBLEDevice::init(HUD_SETTINGS_BLE_NAME);
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    NimBLEDevice::setSecurityPasskey(HUD_SETTINGS_PIN);
    NimBLEDevice::setMTU(247);
    server = NimBLEDevice::createServer();
    server->setCallbacks(&server_callbacks, false);
    server->advertiseOnDisconnect(false);
    auto *service = server->createService(SERVICE);
    for (unsigned i = 0; i < 5; ++i) {
        chars[i] = service->createCharacteristic(UUIDS[i], NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE |
            NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::WRITE_AUTHEN);
        chars[i]->setCallbacks(&setting_callbacks[i]);
        chars[i]->setValue(&state.value[i], 1);
    }
    service->start();
    hud_ble_ota_begin(server, ota_authorized_handle);
    if (!server->start()) abort();
    // The bonded Android may cache the pre-OTA GATT database.
    if (prefs.getUChar("gatt-schema", 0) != 1) {
        server->sendServiceChangedIndication();
        if (prefs.putUChar("gatt-schema", 1) != 1) Serial.println("[settings] Could not save GATT schema marker.");
    }
    Lock lock; advertise();
    Serial.printf("[settings] ready; BOOT GPIO%d, owner=%s\n", HUD_SETTINGS_BOOT_PIN, state.owned ? "saved" : "none");
}
void hud_ble_settings_poll() {
    static bool held = false, fired = false;
    static uint32_t pressed;
    const bool down = digitalRead(HUD_SETTINGS_BOOT_PIN) == LOW;
    if (down && !held) { held = true; fired = false; pressed = millis(); }
    if (down && !fired && (uint32_t)(millis() - pressed) >= HUD_SETTINGS_BOOT_HOLD_MS) {
        Lock lock;
        reset_pending = true; fired = true;
        NimBLEDevice::getAdvertising()->stop();
        Serial.println("[settings] Release BOOT to erase phone binding (settings retained).");
    }
    if (!down && held) {
        held = false;
        if (fired) {
            uint16_t handle;
            { Lock lock; handle = phone_handle; }
            if (handle != BLE_HS_CONN_HANDLE_NONE) {
                server->disconnect(handle);
                delay(100);
            }
            bool saved;
            {
                Lock lock;
                Stored next = state; next.owned = 0; next.type = 0; memset(next.address, 0, sizeof(next.address));
                /* Delete only the settings phone's bond; keep the CAN gateway bond. */
                saved = !state.owned || !NimBLEDevice::isBonded(owner()) || NimBLEDevice::deleteBond(owner());
                if (saved) saved = save(next);
            }
            if (saved) { delay(50); ESP.restart(); }
            else { Serial.println("[settings] Reset failed; staying closed. Retry BOOT."); }
        }
    }
    uint16_t timed_out = BLE_HS_CONN_HANDLE_NONE;
    {
        Lock lock;
        if (phone_handle != BLE_HS_CONN_HANDLE_NONE && !phone_authenticated &&
            (uint32_t)(millis() - phone_since) > HUD_SETTINGS_PAIR_TIMEOUT_MS) timed_out = phone_handle;
        advertise();
    }
    if (timed_out != BLE_HS_CONN_HANDLE_NONE) server->disconnect(timed_out);
    hud_ble_ota_poll();
}
bool hud_ble_gateway_security_begin() {
    Lock lock;
    if (gateway_security || reset_pending || phone_handle != BLE_HS_CONN_HANDLE_NONE || server->getConnectedCount() || hud_ble_ota_busy()) return false;
    gateway_security = true;
    NimBLEDevice::getAdvertising()->stop();
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY);
    return true;
}
void hud_ble_gateway_security_end() {
    Lock lock;
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    NimBLEDevice::setSecurityPasskey(HUD_SETTINGS_PIN);
    gateway_security = false;
    advertise();
}
