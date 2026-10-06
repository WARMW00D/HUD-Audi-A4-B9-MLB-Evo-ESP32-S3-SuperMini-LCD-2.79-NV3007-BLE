#pragma once
#include <stdint.h>
class NimBLEServer;
typedef bool (*HudOtaAuthorize)(uint16_t handle);
void hud_ble_ota_begin(NimBLEServer *server, HudOtaAuthorize authorize);
void hud_ble_ota_disconnect(uint16_t handle);
void hud_ble_ota_poll();
bool hud_ble_ota_busy();
