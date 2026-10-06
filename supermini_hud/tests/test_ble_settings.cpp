/* Host-only simulation using mock APIs; not a real NimBLE/ESP32 build. */
#include <cassert>
#include <cstdio>
#include "../hud_ble_ota.cpp"
#include "../hud_ble_settings.cpp"
static uint8_t live[8];
extern "C" void hud_set_psd(bool b){live[0]=b;}
extern "C" void hud_set_vze(bool b){live[1]=b;}
extern "C" void hud_set_lang(uint8_t v){live[2]=v;}
extern "C" void hud_set_units(uint8_t v){live[3]=v;}
extern "C" void hud_set_gallons(bool b){live[4]=b;}
extern "C" void hud_set_tank_l(uint8_t v){live[5]=v;}
extern "C" void hud_set_accel_bar(bool b){live[6]=b;}
extern "C" void hud_set_overspeed_tol(uint8_t v){live[7]=v;}
static void connect(NimBLEConnInfo &c){server->cb->onConnect(server,c);}
static void auth(NimBLEConnInfo &c){server->cb->onAuthenticationComplete(c);}
static void write(unsigned i,uint8_t v,NimBLEConnInfo&c){chars[i]->setValue(&v,1);chars[i]->cb->onWrite(chars[i],c);}
int main(){
 // Legacy record without extension keys: preserve the existing byte layout.
 Stored legacy={1,{HUD_PSD_LIMITS,HUD_VZE_SIGNS,HUD_LANG,HUD_UNITS,HUD_VOLUME_GAL},0,0,{0}};
 prefs.data.assign((uint8_t*)&legacy,(uint8_t*)&legacy+sizeof(legacy));
 hud_ble_settings_begin();assert(live[5]==HUD_TANK_L&&live[6]==HUD_ACCEL_BAR);assert(!state.owned);assert(!NimBLEDevice::adv.directed);
 NimBLEConnInfo a;a.id=NimBLEAddress("11:22:33:44:55:66",1);connect(a);
 write(5,63,a);write(6,1,a);assert(live[5]==HUD_TANK_L&&live[6]==HUD_ACCEL_BAR);
 write(2,1,a);assert(live[2]==HUD_LANG);assert(!state.owned);
 a.bonded=a.encrypted=a.authenticated=true;NimBLEDevice::bonds.insert(a.id.toString());auth(a);assert(state.owned);
 for(unsigned i=0;i<5;i++){write(i,1,a);assert(live[i]==1);write(i,0,a);assert(live[i]==0);}
 assert(live[7]==20);for(uint8_t tol : {0,1,20,100}){write(7,tol,a);assert(live[7]==tol);}
 write(7,101,a);write(7,255,a);assert(live[7]==100);
 prefs.fail=true;write(7,10,a);assert(live[7]==100);prefs.fail=false;write(7,35,a);
 for(uint8_t capacity : {1,54,63,128,200}) {write(5,capacity,a);assert(live[5]==capacity);}
 write(5,0,a);write(5,201,a);write(5,255,a);assert(live[5]==200);
 uint8_t longPayload[]={54,0};chars[5]->setValue(longPayload,2);chars[5]->cb->onWrite(chars[5],a);assert(live[5]==200);
 prefs.fail=true;write(5,63,a);write(6,1,a);assert(live[5]==200&&live[6]==HUD_ACCEL_BAR);prefs.fail=false;
 write(5,63,a);write(6,1,a);write(6,2,a);assert(live[5]==63&&live[6]==1);
 write(2,2,a);assert(live[2]==0);
 prefs.fail=true;write(2,1,a);assert(live[2]==0);prefs.fail=false;
 Stored saved;memcpy(&saved,prefs.data.data(),sizeof(saved));assert(saved.owned&&saved.value[2]==0);
 NimBLEConnInfo b;b.id=NimBLEAddress("77:88:99:aa:bb:cc",1);b.handle=2;b.bonded=b.encrypted=b.authenticated=true;
 unsigned ds=server->disconnects;connect(b);assert(server->disconnects>ds);write(2,1,b);write(5,80,b);write(6,0,b);write(7,10,b);assert(live[7]==35);assert(live[2]==0&&live[5]==63&&live[6]==1);auth(b);assert(std::string(state.address)==a.id.toString());
 server->cb->onDisconnect(server,a,0);hud_ble_settings_poll();assert(!NimBLEDevice::adv.directed&&NimBLEDevice::adv.filtered&&NimBLEDevice::adv.name.empty());
 // Reconnect with an identity address type from controller resolution.
 const auto identity=a.id.toString();
 a.id=NimBLEAddress("de:ad:be:ef:11:22",1);connect(a);assert(phone_handle==a.handle);
 a.id=NimBLEAddress(identity,3);auth(a);assert(phone_authenticated);
 assert(!hud_ble_gateway_security_begin());
 server->cb->onDisconnect(server,a,0);assert(hud_ble_gateway_security_begin());assert(NimBLEDevice::io==BLE_HS_IO_KEYBOARD_ONLY);hud_ble_gateway_security_end();assert(NimBLEDevice::io==BLE_HS_IO_DISPLAY_ONLY);
 // Simulated reboot: load owner and values from the actual Preferences calls.
 memset(live, 1, sizeof(live));hud_ble_settings_begin();
 assert(state.owned);assert(live[5]==63&&live[6]==1&&live[7]==35);for(unsigned i=0;i<5;i++)assert(live[i]==saved.value[i]);
 assert(!NimBLEDevice::adv.directed&&NimBLEDevice::adv.filtered);
 NimBLEDevice::bonds.insert("aa:bb:cc:dd:ee:ff");
 test_down=true;test_ms=100;hud_ble_settings_poll();test_ms=5099;hud_ble_settings_poll();assert(!reset_pending);test_ms=5100;hud_ble_settings_poll();assert(reset_pending&&!ESP.restarted);test_down=false;hud_ble_settings_poll();assert(ESP.restarted);
 memcpy(&saved,prefs.data.data(),sizeof(saved));assert(!saved.owned&&saved.value[2]==0);assert(prefs.getUChar("speed-tol",0)==35);assert(prefs.getUChar("tank-l",0)==63&&prefs.getUChar("accel-bar",0)==1);assert(!NimBLEDevice::bonds.count(a.id.toString()));assert(NimBLEDevice::bonds.count("aa:bb:cc:dd:ee:ff"));
 // Corrupt optional settings fall back to defaults.
 prefs.bytes["tank-l"]=0;prefs.bytes["accel-bar"]=255;prefs.bytes["speed-tol"]=255;
 hud_ble_settings_begin();assert(live[5]==HUD_TANK_L&&live[6]==HUD_ACCEL_BAR&&live[7]==20);
 puts("PASS: tank/bar bounds, payloads, persistence, failed writes, legacy NVS; settings/NVS simulation, authenticated owner only, invalid writes, flash failure, anonymous filtered mode, delayed resolved identity, gateway pairing serialization, BOOT 5s and gateway bond preservation.");
}
