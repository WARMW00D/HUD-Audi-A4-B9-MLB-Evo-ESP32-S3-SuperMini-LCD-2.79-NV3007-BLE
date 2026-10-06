#pragma once
#include <vector>
#include <cstring>
class Preferences {
public:
 std::vector<unsigned char> data; bool fail=false; uint8_t gatt_marker=0;
 bool begin(const char*,bool){return true;}
 uint8_t getUChar(const char*,uint8_t fallback){return gatt_marker?gatt_marker:fallback;}
 size_t putUChar(const char*,uint8_t n){if(fail)return 0;gatt_marker=n;return 1;}
 size_t putBytes(const char*,const void*p,size_t n){if(fail)return 0;data.assign((const unsigned char*)p,(const unsigned char*)p+n);return n;}
 size_t getBytesLength(const char*){return data.size();}
 size_t getBytes(const char*,void*p,size_t n){memcpy(p,data.data(),n);return n;}
};
