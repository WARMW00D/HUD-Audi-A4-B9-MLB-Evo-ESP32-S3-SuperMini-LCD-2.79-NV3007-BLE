// Host simulation: real SHA-256 via OpenSSL, mocked ESP-IDF flash and NimBLE.
#include <cassert>
#include <cstdio>
#include <new>
#include "../hud_ble_ota.cpp"
#include "../hud_ble_settings.cpp"
extern "C" void hud_set_psd(bool){}
extern "C" void hud_set_vze(bool){}
extern "C" void hud_set_lang(uint8_t){}
extern "C" void hud_set_units(uint8_t){}
extern "C" void hud_set_gallons(bool){}

static NimBLEConnInfo phone;
static std::vector<uint8_t> image(1031,0x42);
static uint8_t hash[32];
static void ctrl(std::vector<uint8_t> value,NimBLEConnInfo &p=phone){
    control_char->setValue(value.data(),value.size());control_char->cb->onWrite(control_char,p);
}
static void packet(uint32_t offset,size_t n,bool corrupt=false){
    std::vector<uint8_t> value(n+4);HudOtaCore::put32(value.data(),offset);
    memcpy(value.data()+4,image.data()+offset,n);if(corrupt)value.back()^=1;
    NimBLECharacteristic c;c.setValue(value.data(),value.size());
    NimBLECharacteristicCallbacks *callback=&data_callbacks;callback->onWrite(&c,phone);
}
static void start(uint32_t size=0){
    std::vector<uint8_t> value(37);value[0]=1;
    HudOtaCore::put32(value.data()+1,size?size:uint32_t(image.size()));memcpy(value.data()+5,hash,32);ctrl(value);
}
static uint8_t error(){uint8_t v[16];core.status(v);return v[2];}
static void fresh(){
    storage.abort();core.~HudOtaCore();new(&core) HudOtaCore(storage);
    session_handle=BLE_HS_CONN_HANDLE_NONE;restart_pending=false;ESP.restarted=false;
    test_write_fail=test_begin_fail=test_image_fail=test_commit_fail=false;test_layout=true;
    test_selected=test_running;test_ms=0;test_commits=0;
}
static void transfer(bool corrupt=false){
    start();assert(core.state()==HudOtaState::Receiving);
    for(size_t offset=0;offset<image.size();offset+=240){
        size_t n=std::min(size_t(240),image.size()-offset);packet(uint32_t(offset),n,corrupt&&offset==240);
        assert(test_selected==test_running);
    }
    ctrl({2});
}
int main(){
    image[0]=0xe9;image[12]=9;image[13]=0;HudOtaCore::put32(image.data()+32,0xabcd5432);
    SHA256(image.data(),image.size(),hash);
    hud_ble_settings_begin();phone.id=NimBLEAddress("11:22:33:44:55:66",1);
    server->cb->onConnect(server,phone);
    phone.bonded=phone.encrypted=phone.authenticated=true;
    NimBLEDevice::bonds.insert(phone.id.toString());server->cb->onAuthenticationComplete(phone);
    assert(phone_authenticated);

    fresh();transfer();assert(core.state()==HudOtaState::Verified);assert(test_flash==image);
    assert(test_selected==test_running&&test_commits==0);ctrl({3});
    assert(core.state()==HudOtaState::Committed&&test_commits==1&&test_selected!=test_running);
    ctrl({3});ctrl({4});assert(test_commits==1&&core.state()==HudOtaState::Committed);
    hud_ble_ota_disconnect(phone.handle);test_ms=1999;hud_ble_ota_poll();assert(!ESP.restarted);
    test_ms=2000;hud_ble_ota_poll();assert(ESP.restarted);

    fresh();transfer(true);assert(error()==6&&test_selected==test_running&&!test_flash_open);
    fresh();test_image_fail=true;transfer();assert(error()==7&&test_commits==0);
    fresh();transfer();test_commit_fail=true;ctrl({3});assert(error()==4&&test_selected==test_running);
    fresh();test_begin_fail=true;start();assert(error()==4&&!test_flash_open);
    fresh();start();test_write_fail=true;packet(0,240);assert(error()==4&&!test_flash_open);
    fresh();start();packet(0,240);packet(0,240);assert(error()==5&&!test_flash_open);
    fresh();start();packet(4,240);assert(error()==5&&test_commits==0);
    fresh();start();packet(0,240);ctrl({2});assert(error()==3&&test_commits==0);
    fresh();start(0x1f0001);assert(error()==3&&!test_flash_open);
    fresh();test_layout=false;start();assert(error()==2);
    fresh();start();packet(0,240);ctrl({4});assert(core.state()==HudOtaState::Idle&&!test_flash_open);
    fresh();start();packet(0,240);hud_ble_ota_disconnect(phone.handle);assert(core.state()==HudOtaState::Idle&&!test_flash_open);
    fresh();start();test_ms=120001;hud_ble_ota_poll();assert(error()==9&&!test_flash_open);
    fresh();start();ctrl({3});assert(error()==8&&test_commits==0);
    fresh();ctrl({1});assert(error()==1&&test_commits==0);
    fresh();start();uint8_t old=image[12];image[12]=0;packet(0,240);image[12]=old;assert(error()==7&&test_flash.empty());
    fresh();start();image[32]^=1;packet(0,240);image[32]^=1;assert(error()==7&&test_flash.empty());
    fresh();start();std::vector<uint8_t> too_big(245);HudOtaCore::put32(too_big.data(),0);
    core.packet(too_big.data(),too_big.size());assert(error()==1&&test_flash.empty());
    fresh();start(100);packet(0,101);assert(error()==3);

    // An unauthorized peer neither reads status nor changes an owner's live session.
    fresh();start();packet(0,240);NimBLEConnInfo other=phone;other.handle=2;other.id=NimBLEAddress("aa:bb:cc:dd:ee:ff",1);
    unsigned disconnected=server->disconnects;ctrl({4},other);
    assert(server->disconnects==disconnected+1&&core.state()==HudOtaState::Receiving);
    hud_ble_ota_disconnect(other.handle);assert(core.state()==HudOtaState::Receiving);
    control_char->cb->onRead(control_char,other);assert(server->disconnects==disconnected+2);
    control_char->cb->onRead(control_char,phone);assert(control_char->v.size()==16);
    auto record=prefs.data;ctrl({4});assert(prefs.data==record); // OTA never rewrites settings/bonds.
    puts("PASS: OTA SHA-256, commit only after full verification, duplicate commit, lost final ACK/reboot, slot selection, invalid image/chip, flash faults, offsets/overflow, cancel/disconnect/timeout, owner authorization, NVS retention.");
}
