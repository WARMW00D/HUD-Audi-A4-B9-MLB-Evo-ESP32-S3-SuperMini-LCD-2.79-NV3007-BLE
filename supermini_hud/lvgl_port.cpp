#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <SPI.h>
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
#ifndef HUD_LCD_MIRROR_X
#define HUD_LCD_MIRROR_X 0
#endif
#if HUD_LCD_MIRROR_X
/* Reverse every row of the strip in place (RGB565, one 16-bit word per pixel). */
static void mirror_rows(lv_color_t *pixels, int w, int h)
{
    uint16_t *p = reinterpret_cast<uint16_t *>(pixels);
    for (int y = 0; y < h; ++y, p += w) {
        for (int i = 0, j = w - 1; i < j; ++i, --j) {
            const uint16_t t = p[i]; p[i] = p[j]; p[j] = t;
        }
    }
}
#endif
#if HUD_PERF_LOG
static volatile int64_t g_t_queued;
static volatile uint32_t g_xfer_us_sum, g_xfer_cnt, g_bytes_sum;
#endif

#if HUD_LCD_ASYNC_DMA
/* ------------------------------------------------------------------------
   Async path: Arduino_GFX only initialises the panel (reset, init table,
   rotation, inversion, clear). After that the Arduino SPI is released and the
   same pins are driven by the ESP-IDF spi_master with a transaction queue and
   DMA. flush_cb queues CASET/RASET/RAMWR + pixels and returns at once; the
   completion ISR calls lv_disp_flush_ready(). With two LVGL draw buffers the
   CPU renders the next strip while the previous one is still being clocked out.
   ------------------------------------------------------------------------ */
#define DMA_HOST       SPI3_HOST
#define TR_CMD         0u
#define TR_DATA        1u
#define TR_LAST        2u   /* last transaction of a flush: completion ISR */
#define N_SLOTS        6

struct TxSlot { spi_transaction_t t; };
static spi_device_handle_t g_dev;
static TxSlot g_slots[N_SLOTS];
static lv_disp_drv_t *g_drv;
static int g_x_start, g_y_start;          /* controller RAM offsets (see Arduino_TFT::setRotation) */
static volatile bool g_inflight;
static int g_pending;                     /* queued transactions whose results are not collected yet */

static void IRAM_ATTR spi_pre_cb(spi_transaction_t *t)
{
    gpio_set_level((gpio_num_t)HUD_LCD_DC, ((uintptr_t)t->user & TR_DATA) ? 1 : 0);
}

static void IRAM_ATTR spi_post_cb(spi_transaction_t *t)
{
    if ((uintptr_t)t->user & TR_LAST) {
#if HUD_PERF_LOG
        g_xfer_us_sum += (uint32_t)(esp_timer_get_time() - g_t_queued);
        g_xfer_cnt++;
#endif
        g_inflight = false;
        lv_disp_flush_ready(g_drv);
    }
}

static void slot_cmd(int i, uint8_t cmd)
{
    spi_transaction_t &t = g_slots[i].t;
    memset(&t, 0, sizeof(t));
    t.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_CS_KEEP_ACTIVE;
    t.length = 8;
    t.tx_data[0] = cmd;
    t.user = (void *)(uintptr_t)TR_CMD;
}

static void slot_d16x2(int i, uint16_t a, uint16_t b)
{
    spi_transaction_t &t = g_slots[i].t;
    memset(&t, 0, sizeof(t));
    t.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_CS_KEEP_ACTIVE;
    t.length = 32;
    t.tx_data[0] = a >> 8; t.tx_data[1] = a & 0xFF;
    t.tx_data[2] = b >> 8; t.tx_data[3] = b & 0xFF;
    t.user = (void *)(uintptr_t)TR_DATA;
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *pixels)
{
    const int w = area->x2 - area->x1 + 1;
    const int h = area->y2 - area->y1 + 1;
    assert(area->x1 >= 0 && area->y1 >= 0 && area->x2 < HUD_PANEL_W && area->y2 < HUD_PANEL_H);
    assert(w * h <= HUD_PANEL_W * HUD_STRIPE_H);
    /* Collect the results of the previous flush (LVGL calls flush_cb only
       after lv_disp_flush_ready(), so all of them are done or finishing). */
    while (g_pending > 0) {
        spi_transaction_t *r;
        if (spi_device_get_trans_result(g_dev, &r, portMAX_DELAY) != ESP_OK) break;
        --g_pending;
    }
#if !LV_COLOR_16_SWAP
    /* Panel wants big-endian RGB565 on the wire. */
    uint32_t *p32 = reinterpret_cast<uint32_t *>(pixels);
    const int n32 = (w * h) >> 1;
    for (int i = 0; i < n32; ++i) {
        uint32_t v = p32[i];
        p32[i] = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);
    }
    if (w * h & 1) {
        uint16_t c = pixels[w * h - 1].full;
        pixels[w * h - 1].full = (c >> 8) | (c << 8);
    }
#endif
#if HUD_LCD_MIRROR_X
    mirror_rows(pixels, w, h);
    const int x0 = (HUD_PANEL_W - 1 - area->x2) + g_x_start, x1 = (HUD_PANEL_W - 1 - area->x1) + g_x_start;
#else
    const int x0 = area->x1 + g_x_start, x1 = area->x2 + g_x_start;
#endif
    const int y0 = HUD_VIEW_Y + area->y1 + g_y_start, y1 = HUD_VIEW_Y + area->y2 + g_y_start;
    slot_cmd(0, 0x2A);  slot_d16x2(1, x0, x1);
    slot_cmd(2, 0x2B);  slot_d16x2(3, y0, y1);
    slot_cmd(4, 0x2C);
    spi_transaction_t &px = g_slots[5].t;
    memset(&px, 0, sizeof(px));
    px.length = (size_t)w * h * 16;
    px.tx_buffer = pixels;
    px.user = (void *)(uintptr_t)(TR_DATA | TR_LAST);
    g_drv = drv;
    g_inflight = true;
#if HUD_PERF_LOG
    g_t_queued = esp_timer_get_time();
    g_bytes_sum += (uint32_t)w * h * 2;
#endif
    /* Diagnostics: do not abort on a rejected transaction, print which one and why
       (first few times only), so the cause can be fixed from the Serial log. */
    int queued = 0;
    for (int i = 0; i < N_SLOTS; ++i) {
        const esp_err_t e = spi_device_queue_trans(g_dev, &g_slots[i].t, portMAX_DELAY);
        if (e != ESP_OK) {
            static int reported;
            if (reported++ < 8)
                Serial.printf("[dma] queue slot %d/%d rejected: %s (0x%x) flags=0x%x length=%u bits rxlength=%u buf=%p area=%d,%d..%d,%d w=%d h=%d\n",
                              i, N_SLOTS, esp_err_to_name(e), (unsigned)e, (unsigned)g_slots[i].t.flags,
                              (unsigned)g_slots[i].t.length, (unsigned)g_slots[i].t.rxlength,
                              g_slots[i].t.tx_buffer, area->x1, area->y1, area->x2, area->y2, w, h);
            break;
        }
        ++queued;
    }
    g_pending = queued;
    if (queued != N_SLOTS) {        /* incomplete flush: release LVGL so it does not hang */
        g_inflight = false;
        lv_disp_flush_ready(drv);
    }
}

static bool dma_port_start(void)
{
    /* Hand the pins over from Arduino SPI to the IDF driver. */
    SPI.end();
    spi_bus_config_t bus = {};
    bus.mosi_io_num = HUD_LCD_MOSI;
    bus.miso_io_num = -1;
    bus.sclk_io_num = HUD_LCD_SCK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = HUD_PANEL_W * HUD_STRIPE_H * 2;
    if (spi_bus_initialize(DMA_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = HUD_LCD_DMA_SPI_HZ;
    dev.mode = 0;
    dev.spics_io_num = HUD_LCD_CS;
    dev.queue_size = N_SLOTS + 2;
    dev.pre_cb = spi_pre_cb;
    dev.post_cb = spi_post_cb;
    if (spi_bus_add_device(DMA_HOST, &dev, &g_dev) != ESP_OK) return false;
    /* SPI_TRANS_CS_KEEP_ACTIVE (used between CASET/RASET/RAMWR and the pixels) is only accepted
       by the IDF driver while the device owns the bus. This is the only device on SPI3,
       so it keeps the bus for good. */
    if (spi_device_acquire_bus(g_dev, portMAX_DELAY) != ESP_OK) return false;
    gpio_set_direction((gpio_num_t)HUD_LCD_DC, GPIO_MODE_OUTPUT);
    /* Offsets exactly as Arduino_TFT::setRotation() picks them for rotation 1/3. */
#if HUD_LCD_ROTATION == 1
    g_x_start = 0;                      g_y_start = HUD_LCD_COL_OFFSET_2;
#else
    g_x_start = 0;                      g_y_start = HUD_LCD_COL_OFFSET_1;
#endif
    return true;
}
#else  /* legacy blocking path */
/* One small internal-RAM buffer; SPI is blocking, so LVGL can reuse it
   only after lv_disp_flush_ready(). Partial areas are tightly packed. */
static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *pixels)
{
    const int w = area->x2 - area->x1 + 1;
    const int h = area->y2 - area->y1 + 1;
    assert(area->x1 >= 0 && area->y1 >= 0 && area->x2 < HUD_PANEL_W && area->y2 < HUD_PANEL_H);
    assert(w * h <= HUD_PANEL_W * HUD_STRIPE_H);
#if HUD_PERF_LOG
    const int64_t t0 = esp_timer_get_time();
#endif
#if LV_COLOR_16_SWAP
    /* Arduino_GFX expects native RGB565, independent of LVGL byte order. */
    for (int i = 0; i < w * h; ++i) {
        uint16_t c = pixels[i].full;
        pixels[i].full = (c >> 8) | (c << 8);
    }
#endif
#if HUD_LCD_MIRROR_X
    mirror_rows(pixels, w, h);
    panel.draw16bitRGBBitmap(HUD_PANEL_W - 1 - area->x2, HUD_VIEW_Y + area->y1,
                            reinterpret_cast<uint16_t *>(pixels), w, h);
#else
    panel.draw16bitRGBBitmap(area->x1, HUD_VIEW_Y + area->y1,
                            reinterpret_cast<uint16_t *>(pixels), w, h);
#endif
#if HUD_PERF_LOG
    g_xfer_us_sum += (uint32_t)(esp_timer_get_time() - t0);
    g_xfer_cnt++;
    g_bytes_sum += (uint32_t)w * h * 2;
#endif
    lv_disp_flush_ready(drv);
}
#endif

static void tick_cb(void *) { lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS); }

#if HUD_PERF_LOG
/* Every HUD_PERF_LOG_MS: lv_timer_handler() time of calls that drew something
   (render + flush call; in async mode the transfer is NOT included), mean
   transfer time per flush (queue -> last byte out), flushes/s and KB/s. */
static void perf_after_handler(uint32_t flushes0, uint32_t us)
{
    static uint32_t n, sum, mx;
    static int64_t t_rep;
    if (g_xfer_cnt != flushes0) { ++n; sum += us; if (us > mx) mx = us; }
    const int64_t now = esp_timer_get_time();
    if (!t_rep) t_rep = now;
    if (now - t_rep < (int64_t)HUD_PERF_LOG_MS * 1000) return;
    const uint32_t xc = g_xfer_cnt, xs = g_xfer_us_sum, by = g_bytes_sum;
    g_xfer_cnt = 0; g_xfer_us_sum = 0; g_bytes_sum = 0;
    const float sec = (now - t_rep) / 1e6f;
    Serial.printf("[perf] refreshes=%u handler avg=%.2f max=%.2f ms | flushes/s=%.0f xfer avg=%.2f ms | %.1f KB/s | spi=%s\n",
        (unsigned)n, n ? sum / 1000.0f / n : 0.f, mx / 1000.0f,
        xc / sec, xc ? xs / 1000.0f / xc : 0.f, by / 1024.0f / sec,
        HUD_LCD_ASYNC_DMA ? "async-dma" : "blocking");
    n = sum = mx = 0; t_rep = now;
}
#endif

static void lvgl_task(void *)
{
    for (;;) {
#if HUD_PERF_LOG
        const uint32_t flushes0 = g_xfer_cnt;
        const int64_t th0 = esp_timer_get_time();
#endif
        uint32_t wait = lv_timer_handler();
#if HUD_PERF_LOG
        perf_after_handler(flushes0, (uint32_t)(esp_timer_get_time() - th0));
#endif
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
#if HUD_LCD_ASYNC_DMA
    const size_t buf_bytes = HUD_PANEL_W * HUD_STRIPE_H * sizeof(lv_color_t);
    lv_color_t *canvas  = static_cast<lv_color_t *>(heap_caps_aligned_alloc(16, buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    lv_color_t *canvas2 = static_cast<lv_color_t *>(heap_caps_aligned_alloc(16, buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    if (!canvas || !canvas2) { Serial.println("ERROR: DMA display buffers allocation failed."); abort(); }
    if (!dma_port_start()) { Serial.println("ERROR: SPI DMA start failed."); abort(); }
    Serial.printf("LCD async SPI DMA: %u Hz, 2 x %u bytes\n", (unsigned)HUD_LCD_DMA_SPI_HZ, (unsigned)buf_bytes);
    lv_disp_draw_buf_init(&draw_buf, canvas, canvas2, HUD_PANEL_W * HUD_STRIPE_H);
#else
    lv_color_t *canvas = static_cast<lv_color_t *>(heap_caps_malloc(
        HUD_PANEL_W * HUD_STRIPE_H * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!canvas) { Serial.println("ERROR: partial display buffer allocation failed."); abort(); }
    lv_disp_draw_buf_init(&draw_buf, canvas, nullptr, HUD_PANEL_W * HUD_STRIPE_H);
#endif
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
