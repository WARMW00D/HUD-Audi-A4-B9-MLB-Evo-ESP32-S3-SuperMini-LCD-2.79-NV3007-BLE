/*
  sim_core.h — ядро симулятора HUD для ПК
  ------------------------------------------------------------------------------
  Код экрана (hud_mockup.c), декодер (can_decode.c), PSD, камеры и анимация стрелок собираются
  из папки прошивки БЕЗ изменений. Здесь — всё, что в прошивке привязано к железу: время, экран,
  звук, датчик света, настройки (в прошивке они приходят из приложения на Android по BLE), а также
  проигрыватель логов сниффера (can_log_NNNN.txt). Ядро не зависит от ОС; окно и файлы — в sim_win.c.
*/
#ifndef SIM_CORE_H
#define SIM_CORE_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SIM_W 428                        /* = HUD_PANEL_W / HUD_PANEL_H из user_config.h (проверяет sim_core.c) */
#define SIM_H 142

/* ---- экран ---- */
extern uint32_t sim_fb[SIM_W * SIM_H];   /* 0x00RRGGBB, сверху вниз */
extern volatile bool sim_fb_dirty;       /* ядро ставит при перерисовке, окно сбрасывает */
extern int sim_backlight;                /* яркость подсветки 0..255, которую выставил HUD; -1 — ещё не выставлял */

uint32_t sim_lv_tick(void);              /* для LV_TICK_CUSTOM */

void sim_init(void);                     /* настройки, LVGL, экран HUD */
void sim_step(uint32_t real_dt_ms);      /* вызывать часто (5-15 мс): время, кадры лога, LVGL */

/* ---- платформа автомобиля (карта CAN-сигналов декодера, HUD_PLATFORM в прошивке) ---- */
#define SIM_PLATFORM_MLB_EVO 1
#define SIM_PLATFORM_MQB     2
void sim_set_platform(int p);            /* можно на ходу: текущая точка лога проигрывается заново новым декодером */
int  sim_platform(void);

/* Нагрузка на экран: сколько раз LVGL перерисовывал экран и сколько пикселей ушло бы в панель (нарастающим итогом),
   lv_ms — время LVGL. Отрисовка частичная, полосами HUD_STRIPE_H — как в прошивке (lvgl_port.cpp). */
void sim_refresh_stats(uint32_t *refreshes, uint32_t *pixels, uint32_t *lv_ms);

/* ---- настройки HUD (то, что в машине меняется из приложения на Android) ---- */
typedef struct {
    uint8_t lang;        /* HUD_LANG_*   */
    uint8_t units;       /* HUD_UNITS_*  */
    uint8_t psd, vze;    /* знаки из PSD / из VZE_01 */
    uint8_t gallons;     /* «сколько заправить»: литры / галлоны */
    uint8_t tank_l;      /* объём бака, л */
    uint8_t accel_bar;   /* бар ускорения */
    uint8_t tol;         /* допуск превышения скорости, км/ч */
    uint8_t cams, cam_beep;
    uint8_t volume;      /* громкость 0..100 */
    uint8_t ldr_on;      /* фоторезистор включён */
    int     ldr_mv;      /* напряжение на нём, мВ (на ПК датчика нет — задаётся вручную) */
} sim_settings_t;
void sim_settings_defaults(sim_settings_t *c);       /* значения по умолчанию из hud_config.h */
void sim_settings_apply(const sim_settings_t *c);    /* применить к HUD теми же hud_set_*, что зовёт прошивка */
void sim_settings_read(sim_settings_t *c);           /* что сейчас в HUD */

/* ---- проигрыватель ---- */
/* Разобрать текст лога (буфер можно освободить сразу после вызова). cont = true — это следующий файл
   той же поездки: если метки времени продолжаются, состояние HUD не сбрасывается. */
bool   sim_load_log(const char *text, size_t len, bool cont);
bool   sim_loaded(void);
void   sim_play(bool on);
bool   sim_playing(void);
void   sim_set_speed(double k);
double sim_speed(void);
uint32_t sim_pos_ms(void);               /* позиция от начала файла */
uint32_t sim_dur_ms(void);
void   sim_seek_ms(uint32_t pos_ms);     /* с «разгоном»: 30 с до точки проигрываются мгновенно */
bool   sim_take_eof(void);               /* файл доигран (флаг сбрасывается при чтении) */
uint32_t sim_frames_total(void);
uint32_t sim_frames_pos(void);

/* метки навигации в файле (из BAP Navigation_SD), время — мс от начала файла */
typedef struct { uint32_t t; uint8_t main, dir; } sim_man_t;
const sim_man_t *sim_maneuvers(uint32_t *count);              /* смены манёвра (fct 0x17) */
typedef struct { uint32_t t0, t1; } sim_span_t;
const sim_span_t *sim_nav_spans(uint32_t *count);             /* ведение по маршруту активно (fct 0x11 = 1) */

/* ---- данные из файлов (буфер должен жить всегда) ---- */
bool sim_set_arrows(const void *buf, size_t len);             /* arrows.bin */
bool sim_set_cams(const void *buf, size_t len);               /* cams.bin */

/* ---- то, что даёт платформа (sim_win.c / sim_headless.c) ---- */
void sim_plat_log(const char *utf8);                          /* строка журнала HUD */
void sim_plat_play(const int16_t *pcm, uint32_t samples, uint32_t rate, uint8_t volume);

#ifdef __cplusplus
}
#endif
#endif
