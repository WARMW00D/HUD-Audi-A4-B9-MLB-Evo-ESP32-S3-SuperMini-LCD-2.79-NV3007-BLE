/*
  hud_cams.cpp — см. hud_cams.h
  Чтение W25Q128: команда 0x03 (Read Data) с 24-битным адресом, потоком до конца базы.
  Запись: Write Enable 0x06, Sector Erase 0x20, Page Program 0x02, ожидание по регистру 0x05.
  DMA работает через небольшие буферы во внутренней RAM, база копируется в PSRAM.
*/
#include <Arduino.h>
#include <string.h>
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "mbedtls/version.h"
#include "user_config.h"
#include "hud_config.h"
#include "hud_log.h"
#include "hud_cams.h"
#include "hud_anim.h"
extern "C" {
#include "cam_alert.h"
}

#if HUD_CAMS

#define FLASH_HOST     SPI2_HOST
#define CHUNK          4096
#define MAX_DB_BYTES   (8u * 1024u * 1024u)       /* защита от мусора в заголовке */

static volatile bool s_ready = false;
static spi_device_handle_t s_dev;
static SemaphoreHandle_t s_lock;                  /* один владелец шины и s_db */
static uint8_t *s_db;                             /* база в PSRAM (её читает cam_alert) */
static size_t s_db_len;
static uint8_t *s_small;                          /* 256 Б DMA-буфер для записи */
static uint32_t s_capacity;                       /* 0 — флеши нет */
static int s_open_state;                          /* 0 — не пробовали, 1 — открыта, -1 — нет */

static bool flash_begin(void)
{
    spi_bus_config_t bus = {};
    bus.mosi_io_num = HUD_FLASH_MOSI;
    bus.miso_io_num = HUD_FLASH_MISO;
    bus.sclk_io_num = HUD_FLASH_SCK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = CHUNK;
    if (spi_bus_initialize(FLASH_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
    spi_device_interface_config_t dev = {};
    dev.command_bits = 8;
    dev.address_bits = 24;
    dev.clock_speed_hz = HUD_FLASH_SPI_HZ;
    dev.mode = 0;
    dev.spics_io_num = HUD_FLASH_CS;
    dev.queue_size = 1;
    dev.flags = SPI_DEVICE_HALFDUPLEX;
    return spi_bus_add_device(FLASH_HOST, &dev, &s_dev) == ESP_OK;
}

/* JEDEC ID (0x9F): 3 байта; у W25Q128 — EF 40 18 (или EF 70 18 у JV-вариантов) */
static bool flash_jedec(uint8_t id[3])
{
    spi_transaction_ext_t t = {};
    t.base.flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_USE_RXDATA;
    t.base.cmd = 0x9F;
    t.command_bits = 8;
    t.address_bits = 0;
    t.base.rxlength = 24;
    if (spi_device_polling_transmit(s_dev, &t.base) != ESP_OK) return false;
    id[0] = t.base.rx_data[0]; id[1] = t.base.rx_data[1]; id[2] = t.base.rx_data[2];
    return true;
}

/* Простая команда без адреса и данных (0x06 Write Enable) */
static bool flash_cmd(uint8_t cmd)
{
    spi_transaction_ext_t t = {};
    t.base.flags = SPI_TRANS_VARIABLE_ADDR;
    t.base.cmd = cmd;
    t.address_bits = 0;
    return spi_device_polling_transmit(s_dev, &t.base) == ESP_OK;
}

/* Ждать конца стирания/записи по биту BUSY регистра состояния 1 */
static bool flash_wait(uint32_t timeout_ms)
{
    uint32_t t0 = millis();
    for (;;) {
        spi_transaction_ext_t t = {};
        t.base.flags = SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_USE_RXDATA;
        t.base.cmd = 0x05;
        t.address_bits = 0;
        t.base.rxlength = 8;
        if (spi_device_polling_transmit(s_dev, &t.base) != ESP_OK) return false;
        if (!(t.base.rx_data[0] & 1)) return true;
        if ((uint32_t)(millis() - t0) > timeout_ms) return false;
        vTaskDelay(1);
    }
}

static bool flash_read(uint32_t addr, uint8_t *dst, size_t len, uint8_t *dma_buf)
{
    unsigned n_chunks = 0;
    while (len) {
        size_t n = len > CHUNK ? CHUNK : len;
        spi_transaction_t t = {};
        t.cmd = 0x03;
        t.addr = addr;
        t.rxlength = n * 8;
        t.rx_buffer = dma_buf;
        if (spi_device_polling_transmit(s_dev, &t) != ESP_OK) return false;
        memcpy(dst, dma_buf, n);
        dst += n; addr += n; len -= n;
        if ((++n_chunks & 3) == 0) vTaskDelay(1);               /* не мешаем экрану и CAN */
    }
    return true;
}

/* Открыть флешь один раз. Вызывать под s_lock. */
static bool flash_open(void)
{
    if (s_open_state) return s_open_state > 0;
    s_open_state = -1;
    s_small = (uint8_t *)heap_caps_malloc(256, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_small) { hud_log_write("[cam] no DMA buffer\n"); return false; }
    if (!flash_begin()) { hud_log_write("[cam] flash SPI init failed\n"); return false; }
    uint8_t id[3] = {0, 0, 0};
    if (!flash_jedec(id) || id[0] != 0xEF || id[2] < 0x14 || id[2] > 0x19) {
        arduino_printf("[cam] W25Q flash not found (JEDEC %02X %02X %02X)\n", id[0], id[1], id[2]);
        return false;
    }
    s_capacity = 1u << id[2];
    s_open_state = 1;
    return true;
}

static void free_db(void)
{
    s_ready = false;
    cam_unload();
    vTaskDelay(pdMS_TO_TICKS(30));                              /* дать дочитать cam_query, если он шёл в этот момент */
    if (s_db) { heap_caps_free(s_db); s_db = NULL; s_db_len = 0; }
}

/* Прочитать базу из флеши, проверить и включить. Вызывать под s_lock, флешь открыта. */
static bool load_locked(const uint8_t *expect_sha, size_t want_len)
{
    bool ok = false;
    uint8_t *dma = (uint8_t *)heap_caps_malloc(CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    uint8_t *buf = NULL;
    do {
        if (!dma) { hud_log_write("[cam] no DMA buffer\n"); break; }
        uint8_t hdr[16];
        if (!flash_read(0, hdr, sizeof hdr, dma) || memcmp(hdr, "CAM1", 4) != 0) {
            hud_log_write("[cam] no CAM1 database at flash address 0\n");
            break;
        }
        uint32_t n; memcpy(&n, hdr + 4, 4);
        size_t len = 16 + (size_t)n * 12;
        if (n == 0 || len > MAX_DB_BYTES || (want_len && len != want_len)) { hud_log_write("[cam] bad database size\n"); break; }
        if (len > s_capacity) { hud_log_write("[cam] database larger than flash\n"); break; }
        buf = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) { arduino_printf("[cam] no PSRAM for %u bytes\n", (unsigned)len); break; }
        if (!flash_read(0, buf, len, dma)) { hud_log_write("[cam] flash read failed\n"); break; }
        if (expect_sha) {
            uint8_t h[32];
            mbedtls_sha256_context c; mbedtls_sha256_init(&c);
#if MBEDTLS_VERSION_MAJOR >= 3
            int r = mbedtls_sha256_starts(&c, 0);
            if (!r) r = mbedtls_sha256_update(&c, buf, len);
            if (!r) r = mbedtls_sha256_finish(&c, h);
#else
            int r = mbedtls_sha256_starts_ret(&c, 0);
            if (!r) r = mbedtls_sha256_update_ret(&c, buf, len);
            if (!r) r = mbedtls_sha256_finish_ret(&c, h);
#endif
            mbedtls_sha256_free(&c);
            if (r || memcmp(h, expect_sha, 32) != 0) { hud_log_write("[cam] flash contents do not match the uploaded SHA-256\n"); break; }
        }
        cam_cfg_t c = { (float)HUD_CAM_AHEAD_S, (float)HUD_CAM_MIN_M, (float)HUD_CAM_MAX_M,
                        (float)HUD_CAM_CROSS_M, (float)HUD_CAM_DIR_DEG, (uint16_t)HUD_CAM_TYPE_MASK };
        cam_config(&c);
        if (!cam_load(buf, len)) { hud_log_write("[cam] cams.bin is corrupt or another version\n"); break; }
        s_db = buf; s_db_len = len; buf = NULL;
        s_ready = true;
        arduino_printf("[cam] database loaded: %u cameras, dated %06u\n", (unsigned)cam_count(), (unsigned)cam_date());
        ok = true;
    } while (0);
    if (buf) heap_caps_free(buf);
    if (dma) heap_caps_free(dma);
    return ok;
}


/* ---- arrows.bin: чтение кадров прямо из флеши ---- */
#if HUD_ANIM
static uint8_t *s_adma;                                         /* DMA-буфер чтения кадров (под s_lock или его владельцем) */

/* Читает для hud_anim: из задачи, которая уже держит s_lock (открытие под замком) — без повторного захвата,
   из задачи LVGL — с коротким ожиданием; false — шина занята (идёт заливка), кадр пропускается. */
static bool anim_read(uint32_t addr, uint8_t *dst, size_t len)
{
    if (!s_lock || s_open_state <= 0) return false;
    bool mine = xSemaphoreGetMutexHolder(s_lock) == xTaskGetCurrentTaskHandle();
    if (!mine && xSemaphoreTake(s_lock, 5) != pdTRUE) return false;
    bool ok = true;
    if (!s_adma) s_adma = (uint8_t *)heap_caps_malloc(CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_adma) ok = false;
    else ok = flash_read(addr, dst, len, s_adma);
    if (!mine) xSemaphoreGive(s_lock);
    return ok;
}

static uint32_t anim_room(void)
{
    return s_capacity > HUD_ANIM_FLASH_ADDR ? s_capacity - HUD_ANIM_FLASH_ADDR : 0;
}

/* Открыть arrows.bin, если он есть. Вызывать под s_lock, флешь открыта. */
static void anim_open_locked(void)
{
    if (hud_anim_open(anim_read, HUD_ANIM_FLASH_ADDR, anim_room()))
        arduino_printf("[anim] arrows.bin loaded: %ux%u, %u bytes\n", (unsigned)hud_anim_width(), (unsigned)hud_anim_height(), (unsigned)hud_anim_size());
    else
        hud_log_write("[anim] no arrows.bin in flash, using static arrows\n");
}
#endif

static void load_task(void *)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (flash_open()) {
        load_locked(NULL, 0);
#if HUD_ANIM
        anim_open_locked();
#endif
    }
    xSemaphoreGive(s_lock);
    vTaskDelete(NULL);
}

extern "C" void hud_cams_start(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return;
    xTaskCreatePinnedToCore(load_task, "hud_cams", 6144, NULL, 1, NULL, 0);
}

extern "C" bool hud_cams_ready(void) { return s_ready; }

extern "C" uint32_t hud_cams_flash_capacity(void)
{
    if (!s_lock) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t c = flash_open() ? s_capacity : 0;
    xSemaphoreGive(s_lock);
    return c;
}

extern "C" uint32_t hud_cams_max_db_bytes(void)
{
    if (!s_lock) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t room = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) + s_db_len;
    uint32_t cap = flash_open() ? s_capacity : 0;
    xSemaphoreGive(s_lock);
    if (room > MAX_DB_BYTES) room = MAX_DB_BYTES;
    return cap < room ? cap : (uint32_t)room;
}

extern "C" bool hud_cams_flash_erase(uint32_t addr)
{
    if (!s_lock || (addr & 4095)) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ok = flash_open() && addr < s_capacity && flash_cmd(0x06);
    if (ok) {
        spi_transaction_t t = {};
        t.cmd = 0x20; t.addr = addr;
        ok = spi_device_polling_transmit(s_dev, &t) == ESP_OK && flash_wait(1000);
    }
    xSemaphoreGive(s_lock);
    return ok;
}

extern "C" bool hud_cams_flash_write(uint32_t addr, const uint8_t *d, size_t n)
{
    if (!s_lock || !n || n > 256 || ((addr & 255) + n) > 256) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ok = flash_open() && addr + n <= s_capacity && flash_cmd(0x06);
    if (ok) {
        memcpy(s_small, d, n);
        spi_transaction_t t = {};
        t.cmd = 0x02; t.addr = addr; t.length = n * 8; t.tx_buffer = s_small;
        ok = spi_device_polling_transmit(s_dev, &t) == ESP_OK && flash_wait(50);
    }
    xSemaphoreGive(s_lock);
    return ok;
}

extern "C" bool hud_cams_reload(const uint8_t *expect_sha, size_t len)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    free_db();
    bool ok = flash_open() && load_locked(expect_sha, len);
    xSemaphoreGive(s_lock);
    return ok;
}

extern "C" uint32_t hud_anim_flash_max_bytes(void)
{
#if HUD_ANIM
    if (!s_lock) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t room = flash_open() ? anim_room() : 0;
    xSemaphoreGive(s_lock);
    return room;
#else
    return 0;
#endif
}

extern "C" bool hud_anim_flash_reload(const uint8_t *expect_sha, size_t len)
{
#if HUD_ANIM
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    hud_anim_close();
    vTaskDelay(pdMS_TO_TICKS(30));                              /* дать LVGL закончить кадр, который уже читался */
    bool ok = flash_open() && len >= 128 && len <= anim_room();
    if (ok && expect_sha) {
        uint8_t *dma = (uint8_t *)heap_caps_malloc(CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        uint8_t *tmp = (uint8_t *)heap_caps_malloc(CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        ok = dma && tmp;
        mbedtls_sha256_context c; mbedtls_sha256_init(&c);
        int r = 0;
#if MBEDTLS_VERSION_MAJOR >= 3
        if (ok) r = mbedtls_sha256_starts(&c, 0);
#else
        if (ok) r = mbedtls_sha256_starts_ret(&c, 0);
#endif
        for (size_t off = 0; ok && !r && off < len; ) {
            size_t n = len - off > CHUNK ? CHUNK : len - off;
            if (!flash_read(HUD_ANIM_FLASH_ADDR + off, tmp, n, dma)) { ok = false; break; }
#if MBEDTLS_VERSION_MAJOR >= 3
            r = mbedtls_sha256_update(&c, tmp, n);
#else
            r = mbedtls_sha256_update_ret(&c, tmp, n);
#endif
            off += n;
        }
        uint8_t h[32];
#if MBEDTLS_VERSION_MAJOR >= 3
        if (ok && !r) r = mbedtls_sha256_finish(&c, h);
#else
        if (ok && !r) r = mbedtls_sha256_finish_ret(&c, h);
#endif
        mbedtls_sha256_free(&c);
        if (ok && (r || memcmp(h, expect_sha, 32) != 0)) { hud_log_write("[anim] flash contents do not match the uploaded SHA-256\n"); ok = false; }
        if (dma) heap_caps_free(dma);
        if (tmp) heap_caps_free(tmp);
    }
    if (ok) {
        ok = hud_anim_open(anim_read, HUD_ANIM_FLASH_ADDR, anim_room()) && hud_anim_size() == len;
        if (!ok) hud_anim_close();
    }
    xSemaphoreGive(s_lock);
    return ok;
#else
    (void)expect_sha; (void)len; return false;
#endif
}

#else
extern "C" void hud_cams_start(void) {}
extern "C" bool hud_cams_ready(void) { return false; }
extern "C" uint32_t hud_cams_flash_capacity(void) { return 0; }
extern "C" uint32_t hud_cams_max_db_bytes(void) { return 0; }
extern "C" bool hud_cams_flash_erase(uint32_t) { return false; }
extern "C" bool hud_cams_flash_write(uint32_t, const uint8_t *, size_t) { return false; }
extern "C" bool hud_cams_reload(const uint8_t *, size_t) { return false; }
extern "C" uint32_t hud_anim_flash_max_bytes(void) { return 0; }
extern "C" bool hud_anim_flash_reload(const uint8_t *, size_t) { return false; }
#endif
