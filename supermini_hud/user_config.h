#ifndef USER_CONFIG_H
#define USER_CONFIG_H

/* GPIO numbers, not connector pin numbers. Edit to match your wiring. */
#define HUD_LCD_SCK      4
#define HUD_LCD_MOSI     5
#define HUD_LCD_CS       2
#define HUD_LCD_DC       7
#define HUD_LCD_RST      6
#define EXAMPLE_PIN_NUM_BK_LIGHT 1
#define HUD_LCD_SPI_HZ   20000000
#define HUD_LCD_ROTATION 1   /* 1 or 3: landscape */
#define HUD_LCD_INVERT   0
#define HUD_BL_ACTIVE_HIGH 1 /* BL is a logic input; use an external driver if needed */
#define HUD_LCD_COL_OFFSET_1 12
#define HUD_LCD_COL_OFFSET_2 14

#define HUD_PANEL_W     428
#define HUD_PANEL_H     142
/* Compact layout, scaled uniformly to the whole 428x142 panel.
   Navigation width: 192 -> 128 before final scaling. */
#define HUD_CANVAS_W    518
#define HUD_CANVAS_H    172
#define HUD_VIEW_H      HUD_PANEL_H
#define HUD_VIEW_Y      0
#define HUD_STRIPE_H    8
#define EXAMPLE_LVGL_TICK_PERIOD_MS 5
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 5
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 50

/* Phone settings (separate from BLE_PASSKEY of the CAN gateway). */
#define HUD_SETTINGS_PIN 482731
#define HUD_SETTINGS_BLE_NAME "HUD-SuperMini"
#define HUD_SETTINGS_BOOT_PIN 0
#define HUD_SETTINGS_BOOT_HOLD_MS 5000
#define HUD_SETTINGS_PAIR_TIMEOUT_MS 60000

#endif
