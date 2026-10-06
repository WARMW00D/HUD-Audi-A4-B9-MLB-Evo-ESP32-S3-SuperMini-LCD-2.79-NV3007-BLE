#pragma once
#include <vector>
#include <cstring>
#include <map>
#include <string>
class Preferences {
public:
 std::vector<unsigned char> data; bool fail=false;
 std::map<std::string,uint8_t> bytes;
 bool begin(const char*,bool){return true;}
 uint8_t getUChar(const char* key,uint8_t fallback){auto i=bytes.find(key);return i==bytes.end()?fallback:i->second;}
 size_t putUChar(const char* key,uint8_t n){if(fail)return 0;bytes[key]=n;return 1;}
 size_t putBytes(const char*,const void*p,size_t n){if(fail)return 0;data.assign((const unsigned char*)p,(const unsigned char*)p+n);return n;}
 size_t getBytesLength(const char*){return data.size();}
 size_t getBytes(const char*,void*p,size_t n){memcpy(p,data.data(),n);return n;}
};
