/* HUD v29.1.9 port: ESP32-S3 Super Mini 4 MB / 2 MB, NV3007 428x142. */
#include "user_config.h"
#include "lvgl_port.h"
#include "lvgl.h"
#include "hud_config.h"
#include "hud_source.h"
#include "hud_ble_settings.h"
#include "hud_log.h"
#include "src/lcd_bl_bsp/lcd_bl_pwm_bsp.h"

void lvgl_log_cb(const char *buf)
{
    if (HUD_LOG_LVGL) { hud_log_write("[LVGL] "); hud_log_write(buf); }
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println("[fw] HUD Super Mini v29.1.9 / BLE OTA");
    /* Legacy duty convention: 255 = dark, 0 = full brightness. */
    lcd_bl_pwm_bsp_init(255);
    hud_log_write("=== Super Mini / NV3007 HUD ===\n");
#if LV_USE_LOG
    lv_log_register_print_cb(lvgl_log_cb);
#endif
    hud_ble_settings_begin();
    lvgl_port_init();
    setUpduty(255 - HUD_BRIGHT_NO_DATA);
    hud_source_start();
}

void loop() { hud_ble_settings_poll(); vTaskDelay(pdMS_TO_TICKS(20)); }
