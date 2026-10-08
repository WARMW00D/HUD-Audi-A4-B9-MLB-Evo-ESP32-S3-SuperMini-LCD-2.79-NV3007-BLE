#pragma once
#include <vector>
#include <cstring>
#include <map>
#include <string>
class Preferences {
public:
 std::vector<unsigned char> data; bool fail=false;
 std::map<std::string,uint8_t> bytes; std::map<std::string,uint16_t> ushorts; std::map<std::string,bool> bools;
 bool begin(const char*,bool){return true;}
 uint8_t getUChar(const char* key,uint8_t fallback){auto i=bytes.find(key);return i==bytes.end()?fallback:i->second;}
 size_t putUChar(const char* key,uint8_t n){if(fail)return 0;bytes[key]=n;return 1;}
 uint16_t getUShort(const char* key,uint16_t fallback){auto i=ushorts.find(key);return i==ushorts.end()?fallback:i->second;}
 size_t putUShort(const char* key,uint16_t n){if(fail)return 0;ushorts[key]=n;return 2;}
 bool getBool(const char* key,bool fallback){auto i=bools.find(key);return i==bools.end()?fallback:i->second;}
 size_t putBool(const char* key,bool n){if(fail)return 0;bools[key]=n;return 1;}
 size_t putBytes(const char*,const void*p,size_t n){if(fail)return 0;data.assign((const unsigned char*)p,(const unsigned char*)p+n);return n;}
 size_t getBytesLength(const char*){return data.size();}
 size_t getBytes(const char*,void*p,size_t n){memcpy(p,data.data(),n);return n;}
};
