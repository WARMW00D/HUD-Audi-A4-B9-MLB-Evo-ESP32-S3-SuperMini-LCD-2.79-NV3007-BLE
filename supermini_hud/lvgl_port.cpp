#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "hud_mockup.h"
#include "user_config.h"

#if LVGL_VERSION_MAJOR != 8
#error "This project requires LVGL 8.x (8.4.0 recommended)."
#endif
#if LV_COLOR_DEPTH != 16
#error "Set LV_COLOR_DEPTH 16 in lv_conf.h."
#endif
#if HUD_LCD_ROTATION != 1 && HUD_LCD_ROTATION != 3
#error "HUD_LCD_ROTATION must be 1 or 3 (landscape)."
#endif

/* Use Arduino SPI API rather than Arduino_ESP32SPIDMA internals. */
static Arduino_HWSPI bus(HUD_LCD_DC, HUD_LCD_CS, HUD_LCD_SCK, HUD_LCD_MOSI, -1);
static Arduino_NV3007 panel(&bus, HUD_LCD_RST, HUD_LCD_ROTATION, false,
    142, 428, HUD_LCD_COL_OFFSET_1, 0, HUD_LCD_COL_OFFSET_2, 0,
    nv3007_279_init_operations, sizeof(nv3007_279_init_operations));
/* One small internal-RAM buffer; SPI is blocking, so LVGL can reuse it
   only after lv_disp_flush_ready(). Partial areas are tightly packed. */
static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *pixels)
{
    const int w = area->x2 - area->x1 + 1;
    const int h = area->y2 - area->y1 + 1;
    assert(area->x1 >= 0 && area->y1 >= 0 && area->x2 < HUD_PANEL_W && area->y2 < HUD_PANEL_H);
    assert(w * h <= HUD_PANEL_W * HUD_STRIPE_H);
#if LV_COLOR_16_SWAP
    /* Arduino_GFX expects native RGB565, independent of LVGL byte order. */
    for (int i = 0; i < w * h; ++i) {
        uint16_t c = pixels[i].full;
        pixels[i].full = (c >> 8) | (c << 8);
    }
#endif
    panel.draw16bitRGBBitmap(area->x1, HUD_VIEW_Y + area->y1,
                            reinterpret_cast<uint16_t *>(pixels), w, h);
    lv_disp_flush_ready(drv);
}

static void tick_cb(void *) { lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS); }

static void lvgl_task(void *)
{
    for (;;) {
        uint32_t wait = lv_timer_handler();
        if (wait < EXAMPLE_LVGL_TASK_MIN_DELAY_MS) wait = EXAMPLE_LVGL_TASK_MIN_DELAY_MS;
        if (wait > EXAMPLE_LVGL_TASK_MAX_DELAY_MS) wait = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

void lvgl_port_init(void)
{
    Serial.printf("LCD pins: SCK=%d MOSI=%d CS=%d DC=%d RST=%d BL=%d; SPI=%u Hz\n",
        HUD_LCD_SCK, HUD_LCD_MOSI, HUD_LCD_CS, HUD_LCD_DC, HUD_LCD_RST,
        EXAMPLE_PIN_NUM_BK_LIGHT, (unsigned)HUD_LCD_SPI_HZ);
    if (!psramFound()) {
        Serial.println("ERROR: PSRAM unavailable. Select QSPI PSRAM (2 MB).");
        abort();
    }
    if (!panel.begin(HUD_LCD_SPI_HZ)) {
        Serial.println("ERROR: NV3007 SPI initialization failed.");
        abort();
    }
    Serial.println("NV3007 2.79 init commands sent (no display readback).");
    panel.invertDisplay(HUD_LCD_INVERT);
    panel.fillScreen(0);
    assert(panel.width() == HUD_PANEL_W && panel.height() == HUD_PANEL_H);
    lv_init();
    static lv_disp_draw_buf_t draw_buf;
    static lv_disp_drv_t drv;
    lv_color_t *canvas = static_cast<lv_color_t *>(heap_caps_malloc(
        HUD_PANEL_W * HUD_STRIPE_H * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!canvas) { Serial.println("ERROR: partial display buffer allocation failed."); abort(); }
    lv_disp_draw_buf_init(&draw_buf, canvas, nullptr, HUD_PANEL_W * HUD_STRIPE_H);
    lv_disp_drv_init(&drv);
    drv.hor_res = HUD_CANVAS_W;
    drv.ver_res = HUD_CANVAS_H;
    drv.draw_buf = &draw_buf;
    drv.flush_cb = flush_cb;
    drv.full_refresh = 0;
    drv.direct_mode = 0;
    lv_disp_drv_register(&drv);
    /* Finish all UI construction before giving ownership to the LVGL task. */
    build_hud_mockup();
    esp_timer_create_args_t args = {};
    args.callback = tick_cb;
    args.name = "lvgl_tick";
    esp_timer_handle_t tick;
    ESP_ERROR_CHECK(esp_timer_create(&args, &tick));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));
    if (xTaskCreatePinnedToCore(lvgl_task, "LVGL", 6144, nullptr, 4, nullptr, 0) != pdPASS) abort();
    Serial.printf("Display native/partial: %dx%d, view %dx%d, PSRAM free=%u\n",
        HUD_PANEL_W, HUD_PANEL_H, HUD_PANEL_W, HUD_VIEW_H, (unsigned)ESP.getFreePsram());
}
