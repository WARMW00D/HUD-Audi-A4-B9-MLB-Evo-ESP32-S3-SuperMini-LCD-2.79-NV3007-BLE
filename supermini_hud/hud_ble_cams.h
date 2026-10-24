#pragma once
#include <stdint.h>
class NimBLEServer;
typedef bool (*HudCamsAuthorize)(uint16_t handle);
/* BLE-загрузка cams.bin во флешь W25Q128 (протокол как у OTA, свой сервис). См. docs/CAMS_UPLOAD_ru.md */
void hud_ble_cams_begin(NimBLEServer *server, HudCamsAuthorize authorize);
void hud_ble_cams_disconnect(uint16_t handle);
void hud_ble_cams_poll();
bool hud_ble_cams_busy();
