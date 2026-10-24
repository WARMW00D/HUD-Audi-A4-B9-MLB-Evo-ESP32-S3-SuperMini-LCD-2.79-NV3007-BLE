#ifndef USER_CONFIG_H
#define USER_CONFIG_H

/* GPIO numbers, not connector pin numbers. Edit to match your wiring. */
#define HUD_LCD_SCK              1
#define HUD_LCD_MOSI             2
#define HUD_LCD_CS               5
#define HUD_LCD_DC               4
#define HUD_LCD_RST              3
#define EXAMPLE_PIN_NUM_BK_LIGHT 6
#define HUD_LCD_SPI_HZ   20000000   /* init + legacy blocking path */
#define HUD_LCD_ASYNC_DMA 1          /* 1: queued SPI DMA + 2 draw buffers (render overlaps transfer); 0: old blocking Arduino_GFX path */
#define HUD_LCD_DMA_SPI_HZ 20000000  /* async path clock; try 40000000 and watch the picture for glitches */
#define HUD_PERF_LOG     0           /* print [perf] render/transfer timings to Serial */
#define HUD_PERF_LOG_MS  5000
/* Speaker: passive buzzer/PC speaker through an NPN (BC547), see hud_sound.h */
#define HUD_SOUND_ENABLE   1
#define HUD_SND_PIN        7
#define HUD_SOUND_DUTY_PCT 50     /* 1..50; 50 = loudest, lower = quieter */
#define HUD_SOUND_STARTUP  1      /* rising chirp once the UI is up */

/* W25Q128 flash with the camera database: standard FSPI pins, its own SPI2 bus (hud_cams.cpp).
   cams.bin is stored raw from flash address 0. */
#define HUD_FLASH_CS       10
#define HUD_FLASH_MOSI     11
#define HUD_FLASH_SCK      12
#define HUD_FLASH_MISO     13
#define HUD_FLASH_SPI_HZ   20000000

#define HUD_LCD_ROTATION 1   /* 1 or 3: landscape */
#define HUD_LCD_INVERT   0
#define HUD_BL_ACTIVE_HIGH 1 /* BL is a logic input; use an external driver if needed */
#define HUD_LCD_COL_OFFSET_1 12
#define HUD_LCD_COL_OFFSET_2 14

#define HUD_PANEL_W     428
#define HUD_PANEL_H     142
/* Compact layout, scaled uniformly to the whole 428x142 panel.
   Navigation width: 192 -> 128 before final scaling. */
#define HUD_CANVAS_W    HUD_PANEL_W
#define HUD_CANVAS_H    HUD_PANEL_H
#define HUD_VIEW_H      HUD_PANEL_H
#define HUD_VIEW_Y      0
#define HUD_STRIPE_H    16
#define HUD_LAYOUT_W    518
#define HUD_LAYOUT_H    172
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
