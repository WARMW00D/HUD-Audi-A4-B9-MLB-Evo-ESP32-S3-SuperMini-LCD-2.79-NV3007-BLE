/* Host-only simulation using mock APIs; not a real NimBLE/ESP32 build. */
#include <cassert>
#include <cstdio>
#include "../hud_ble_ota.cpp"
#include "../hud_ble_settings.cpp"
static uint8_t live[5];
extern "C" void hud_set_psd(bool b){live[0]=b;}
extern "C" void hud_set_vze(bool b){live[1]=b;}
extern "C" void hud_set_lang(uint8_t v){live[2]=v;}
extern "C" void hud_set_units(uint8_t v){live[3]=v;}
extern "C" void hud_set_gallons(bool b){live[4]=b;}
static void connect(NimBLEConnInfo &c){server->cb->onConnect(server,c);}
static void auth(NimBLEConnInfo &c){server->cb->onAuthenticationComplete(c);}
static void write(unsigned i,uint8_t v,NimBLEConnInfo&c){chars[i]->setValue(&v,1);chars[i]->cb->onWrite(chars[i],c);}
int main(){
 hud_ble_settings_begin();assert(!state.owned);assert(!NimBLEDevice::adv.directed);
 NimBLEConnInfo a;a.id=NimBLEAddress("11:22:33:44:55:66",1);connect(a);
 write(2,1,a);assert(live[2]==HUD_LANG);assert(!state.owned);
 a.bonded=a.encrypted=a.authenticated=true;NimBLEDevice::bonds.insert(a.id.toString());auth(a);assert(state.owned);
 for(unsigned i=0;i<5;i++){write(i,1,a);assert(live[i]==1);write(i,0,a);assert(live[i]==0);}
 write(2,2,a);assert(live[2]==0);
 prefs.fail=true;write(2,1,a);assert(live[2]==0);prefs.fail=false;
 Stored saved;memcpy(&saved,prefs.data.data(),sizeof(saved));assert(saved.owned&&saved.value[2]==0);
 NimBLEConnInfo b;b.id=NimBLEAddress("77:88:99:aa:bb:cc",1);b.handle=2;b.bonded=b.encrypted=b.authenticated=true;
 unsigned ds=server->disconnects;connect(b);assert(server->disconnects>ds);write(2,1,b);assert(live[2]==0);auth(b);assert(std::string(state.address)==a.id.toString());
 server->cb->onDisconnect(server,a,0);hud_ble_settings_poll();assert(!NimBLEDevice::adv.directed&&NimBLEDevice::adv.filtered&&NimBLEDevice::adv.name.empty());
 // Reconnect with an identity address type from controller resolution.
 const auto identity=a.id.toString();
 a.id=NimBLEAddress("de:ad:be:ef:11:22",1);connect(a);assert(phone_handle==a.handle);
 a.id=NimBLEAddress(identity,3);auth(a);assert(phone_authenticated);
 assert(!hud_ble_gateway_security_begin());
 server->cb->onDisconnect(server,a,0);assert(hud_ble_gateway_security_begin());assert(NimBLEDevice::io==BLE_HS_IO_KEYBOARD_ONLY);hud_ble_gateway_security_end();assert(NimBLEDevice::io==BLE_HS_IO_DISPLAY_ONLY);
 // Simulated reboot: load owner and values from the actual Preferences calls.
 memset(live, 1, sizeof(live));hud_ble_settings_begin();
 assert(state.owned);for(unsigned i=0;i<5;i++)assert(live[i]==saved.value[i]);
 assert(!NimBLEDevice::adv.directed&&NimBLEDevice::adv.filtered);
 NimBLEDevice::bonds.insert("aa:bb:cc:dd:ee:ff");
 test_down=true;test_ms=100;hud_ble_settings_poll();test_ms=5099;hud_ble_settings_poll();assert(!reset_pending);test_ms=5100;hud_ble_settings_poll();assert(reset_pending&&!ESP.restarted);test_down=false;hud_ble_settings_poll();assert(ESP.restarted);
 memcpy(&saved,prefs.data.data(),sizeof(saved));assert(!saved.owned&&saved.value[2]==0);assert(!NimBLEDevice::bonds.count(a.id.toString()));assert(NimBLEDevice::bonds.count("aa:bb:cc:dd:ee:ff"));
 puts("PASS: settings/NVS simulation, authenticated owner only, invalid writes, flash failure, anonymous filtered mode, delayed resolved identity, gateway pairing serialization, BOOT 5s and gateway bond preservation.");
}
