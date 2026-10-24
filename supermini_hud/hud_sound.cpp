#include <Arduino.h>
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "user_config.h"
#include "hud_sound.h"

#if HUD_SOUND_ENABLE

struct Note { uint16_t hz; uint16_t ms; };   /* hz == 0: pause */

static const Note kBeep[]   = {{2400, 80}};
static const Note kDouble[] = {{2400, 70}, {0, 70}, {2400, 70}};
static const Note kRising[] = {{1500, 80}, {0, 20}, {2000, 80}, {0, 20}, {2600, 120}};
static const Note kWarn[]   = {{3000, 110}, {0, 70}, {3000, 110}, {0, 70}, {3000, 110}};

struct Pattern { const Note *n; uint8_t len; };
#define PAT(a) { a, (uint8_t)(sizeof(a) / sizeof(a[0])) }
static const Pattern kPatterns[HUD_SND_COUNT] = { PAT(kBeep), PAT(kDouble), PAT(kRising), PAT(kWarn) };

#define SND_TIMER   LEDC_TIMER_2
#define SND_CHANNEL LEDC_CHANNEL_2      /* backlight uses timer 3 / channel 1 */
#define SND_BITS    LEDC_TIMER_10_BIT

static QueueHandle_t g_q;
static volatile bool g_enabled = true;
static uint32_t g_duty_on;
static volatile uint8_t g_volume = 100;

static void tone(uint32_t hz)
{
    if (hz == 0) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, SND_CHANNEL, 0);
    } else {
        ledc_set_freq(LEDC_LOW_SPEED_MODE, SND_TIMER, hz);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, SND_CHANNEL, g_duty_on);
    }
    ledc_update_duty(LEDC_LOW_SPEED_MODE, SND_CHANNEL);
}

static void sound_task(void *)
{
    for (;;) {
        uint8_t id;
        if (xQueueReceive(g_q, &id, portMAX_DELAY) != pdTRUE || id >= HUD_SND_COUNT) continue;
        const Pattern &p = kPatterns[id];
        for (uint8_t i = 0; i < p.len && g_enabled; ++i) {
            tone(p.n[i].hz);
            vTaskDelay(pdMS_TO_TICKS(p.n[i].ms));
        }
        tone(0);
    }
}

void hud_sound_begin(void)
{
    /* 50 % duty is the loudest square wave; HUD_SOUND_DUTY_PCT <= 50 sets the start-up volume. */
    hud_sound_set_volume((uint8_t)((HUD_SOUND_DUTY_PCT > 50 ? 50 : HUD_SOUND_DUTY_PCT) * 2));
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = SND_BITS;
    timer.timer_num = SND_TIMER;
    timer.freq_hz = 2000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ledc_channel_config_t ch = {};
    ch.gpio_num = HUD_SND_PIN;
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel = SND_CHANNEL;
    ch.intr_type = LEDC_INTR_DISABLE;
    ch.timer_sel = SND_TIMER;
    ch.duty = 0;
    ch.hpoint = 0;
    if (ledc_timer_config(&timer) != ESP_OK || ledc_channel_config(&ch) != ESP_OK) {
        Serial.println("[snd] LEDC init failed");
        return;
    }
    g_q = xQueueCreate(4, sizeof(uint8_t));
    if (!g_q || xTaskCreate(sound_task, "snd", 2048, nullptr, 1, nullptr) != pdPASS) {
        Serial.println("[snd] task start failed");
        g_q = nullptr;
    }
}

void hud_sound_play(hud_sound_t s)
{
    if (!g_q || !g_enabled || !g_volume) return;
    uint8_t id = (uint8_t)s;
    xQueueSend(g_q, &id, 0);
}

void hud_sound_set_enabled(bool on) { g_enabled = on; }
/* Volume 0..100 %: 0 is silent; 100 is a 50 % duty square wave. The curve is quadratic
   because the ear follows the (narrower) pulse width much less than linearly. */
void hud_sound_set_volume(uint8_t pct)
{
    if (pct > 100) pct = 100;
    g_volume = pct;
    g_duty_on = ((1u << 10) * 50u * pct * pct) / (100u * 10000u);
}
uint8_t hud_sound_volume(void) { return g_volume; }
bool hud_sound_enabled(void) { return g_enabled; }

#else
void hud_sound_begin(void) {}
void hud_sound_play(hud_sound_t) {}
void hud_sound_set_enabled(bool) {}
void hud_sound_set_volume(uint8_t) {}
uint8_t hud_sound_volume(void) { return 0; }
bool hud_sound_enabled(void) { return false; }
#endif
