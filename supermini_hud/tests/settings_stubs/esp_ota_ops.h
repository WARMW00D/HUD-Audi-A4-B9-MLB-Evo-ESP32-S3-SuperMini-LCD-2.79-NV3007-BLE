#pragma once
// Host-only partition/OTA mock; does not validate an actual ESP-IDF image.
#include <cstdint>
#include <vector>
#include <cassert>
using esp_ota_handle_t=uint32_t;
#define ESP_OK 0
#define OTA_WITH_SEQUENTIAL_WRITES ((size_t)-2)
#define ESP_PARTITION_TYPE_APP 0
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_OTA 0
#define ESP_PARTITION_SUBTYPE_APP_OTA_0 16
#define ESP_PARTITION_SUBTYPE_APP_OTA_1 17
struct esp_partition_t{uint32_t address,size;};
static const esp_partition_t test_app0{0x10000,0x1f0000},test_app1{0x200000,0x1f0000},test_otadata{0xe000,0x2000};
static const esp_partition_t *test_running=&test_app0,*test_selected=&test_app0,*test_target=nullptr;
static bool test_layout=true,test_flash_open=false,test_write_fail=false,test_begin_fail=false,test_image_fail=false,test_commit_fail=false;
static unsigned test_commits=0,test_aborts=0,test_begins=0;
static std::vector<uint8_t> test_flash;
inline const esp_partition_t*esp_partition_find_first(int type,int subtype,const char*){
 if(!test_layout)return nullptr;
 if(type==ESP_PARTITION_TYPE_DATA&&subtype==ESP_PARTITION_SUBTYPE_DATA_OTA)return &test_otadata;
 if(type==ESP_PARTITION_TYPE_APP&&subtype==ESP_PARTITION_SUBTYPE_APP_OTA_0)return &test_app0;
 if(type==ESP_PARTITION_TYPE_APP&&subtype==ESP_PARTITION_SUBTYPE_APP_OTA_1)return &test_app1;
 return nullptr;
}
inline const esp_partition_t*esp_ota_get_running_partition(){return test_running;}
inline const esp_partition_t*esp_ota_get_next_update_partition(const esp_partition_t*){return test_layout?(test_running==&test_app0?&test_app1:&test_app0):nullptr;}
inline int esp_ota_begin(const esp_partition_t*p,size_t n,esp_ota_handle_t*h){
 assert(p!=test_running);assert(n==OTA_WITH_SEQUENTIAL_WRITES);++test_begins;
 if(test_begin_fail)return -1;test_target=p;test_flash.clear();test_flash_open=true;*h=1;return ESP_OK;
}
inline int esp_ota_write(esp_ota_handle_t,const uint8_t*p,size_t n){
 assert(test_flash_open);if(test_write_fail)return -1;
 if(test_flash.size()+n>test_target->size)return -1;test_flash.insert(test_flash.end(),p,p+n);return ESP_OK;
}
inline int esp_ota_end(esp_ota_handle_t){assert(test_flash_open);test_flash_open=false;return test_image_fail?-1:ESP_OK;}
inline int esp_ota_abort(esp_ota_handle_t){assert(test_flash_open);test_flash_open=false;++test_aborts;return ESP_OK;}
inline int esp_ota_set_boot_partition(const esp_partition_t*p){
 assert(!test_flash_open&&p!=test_running);if(test_commit_fail)return -1;test_selected=p;++test_commits;return ESP_OK;
}
