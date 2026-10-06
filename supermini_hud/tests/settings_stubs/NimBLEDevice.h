#pragma once
#include <string>
#include <vector>
#include <set>
#include <cstdint>
#define BLE_HS_CONN_HANDLE_NONE 65535
#define BLE_HS_IO_DISPLAY_ONLY 0
#define BLE_HS_IO_KEYBOARD_ONLY 2
#define BLE_GAP_DISC_MODE_NON 0
#define BLE_GAP_DISC_MODE_GEN 2
#define BLE_GAP_CONN_MODE_DIR 1
#define BLE_GAP_CONN_MODE_UND 2
namespace NIMBLE_PROPERTY {enum {READ=1,WRITE=2,READ_AUTHEN=4,WRITE_AUTHEN=8};}
class NimBLEAddress {std::string s;uint8_t t;public:NimBLEAddress(std::string v="",uint8_t x=0):s(v),t(x){}std::string toString()const{return s;}uint8_t getType()const{return t;}};
class NimBLEConnInfo {public:NimBLEAddress id;uint16_t handle=1;bool bonded=false,encrypted=false,authenticated=false;
 NimBLEAddress getIdAddress()const{return id;}uint16_t getConnHandle()const{return handle;}
 bool isBonded()const{return bonded;}bool isEncrypted()const{return encrypted;}bool isAuthenticated()const{return authenticated;}};
class NimBLEServer;class NimBLECharacteristic;
class NimBLEServerCallbacks {public:virtual ~NimBLEServerCallbacks(){}virtual void onConnect(NimBLEServer*,NimBLEConnInfo&){}virtual void onDisconnect(NimBLEServer*,NimBLEConnInfo&,int){}virtual void onAuthenticationComplete(NimBLEConnInfo&){}virtual uint32_t onPassKeyDisplay(){return 0;}};
class NimBLECharacteristicCallbacks {public:virtual ~NimBLECharacteristicCallbacks(){}virtual void onRead(NimBLECharacteristic*,NimBLEConnInfo&){}virtual void onWrite(NimBLECharacteristic*,NimBLEConnInfo&){};};
class NimBLECharacteristic {public:std::vector<uint8_t> v;NimBLECharacteristicCallbacks*cb=nullptr;void setValue(const uint8_t*p,size_t n){v.clear();if(n)v.assign(p,p+n);}void setCallbacks(NimBLECharacteristicCallbacks*p){cb=p;}std::vector<uint8_t> getValue(){return v;}};
class NimBLEService {public:std::vector<NimBLECharacteristic*> c;NimBLECharacteristic*createCharacteristic(const char*,unsigned,uint16_t=512){auto*p=new NimBLECharacteristic;c.push_back(p);return p;}void start(){}};
class NimBLEServer {public:NimBLEServerCallbacks*cb=nullptr;unsigned disconnects=0;void setCallbacks(NimBLEServerCallbacks*p,bool){cb=p;}void advertiseOnDisconnect(bool){}NimBLEService*createService(const char*){return new NimBLEService;}bool start(){return true;}void sendServiceChangedIndication(){}unsigned getConnectedCount(){return 0;}bool disconnect(NimBLEConnInfo&){++disconnects;return true;}bool disconnect(uint16_t){++disconnects;return true;}};
class NimBLEAdvertising {public:bool active=false,directed=false,filtered=false;std::string name,uuid;uint8_t mode=2;unsigned discoverable=2;
 bool isAdvertising(){return active;}bool reset(){active=false;name.clear();uuid.clear();return true;}void enableScanResponse(bool){}void setScanFilter(bool,bool c){filtered=c;}void setDiscoverableMode(unsigned d){discoverable=d;}void setConnectableMode(uint8_t m){mode=m;}void setName(const char*s){name=s;}void addServiceUUID(const char*s){uuid=s;}bool start(unsigned=0,const NimBLEAddress*p=nullptr){directed=p!=nullptr;active=true;return true;}bool stop(){active=false;return true;}};
class NimBLEDevice {public:static NimBLEAdvertising adv;static NimBLEServer srv;static std::set<std::string> bonds,whitelist;static int io;static unsigned pin;
 static void init(const char*){}static void setSecurityAuth(bool,bool,bool){}static void setSecurityIOCap(int n){io=n;}static void setSecurityPasskey(unsigned n){pin=n;}static void setMTU(unsigned){}static NimBLEServer*createServer(){return &srv;}static NimBLEAdvertising*getAdvertising(){return &adv;}static bool startSecurity(uint16_t){return true;}static bool onWhiteList(const NimBLEAddress&a){return whitelist.count(a.toString());}static bool whiteListAdd(const NimBLEAddress&a){whitelist.insert(a.toString());return true;}static bool isBonded(const NimBLEAddress&a){return bonds.count(a.toString());}static bool deleteBond(const NimBLEAddress&a){bonds.erase(a.toString());return true;}};
NimBLEAdvertising NimBLEDevice::adv;NimBLEServer NimBLEDevice::srv;std::set<std::string>NimBLEDevice::bonds,NimBLEDevice::whitelist;int NimBLEDevice::io;unsigned NimBLEDevice::pin;
