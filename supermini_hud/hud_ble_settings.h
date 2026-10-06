#ifndef HUD_BLE_SETTINGS_H
#define HUD_BLE_SETTINGS_H
/* Initialize once, before starting the CAN BLE client; also loads NVS settings. */
void hud_ble_settings_begin();
/* Call every ~20 ms from Arduino loop (BOOT, advertising, pairing timeout). */
void hud_ble_settings_poll();
/* NimBLE IO capability is global. Temporarily serialize gateway pairing
   against phone pairing, preserving existing encrypted phone/gateway links. */
bool hud_ble_gateway_security_begin();
void hud_ble_gateway_security_end();
#endif
