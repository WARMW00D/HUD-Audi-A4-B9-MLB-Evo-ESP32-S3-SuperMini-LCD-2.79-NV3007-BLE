/*
  sim_core.c — см. sim_core.h
*/
#include "lvgl.h"
#include "sim_core.h"
#include "esp_timer.h"
#include "user_config.h"
#include "hud_config.h"
#include "hud_mockup.h"
#include "hud_data.h"
#include "hud_sound.h"
#include "hud_light.h"
#include "hud_source.h"
#include "hud_log.h"
#include "src/lcd_bl_bsp/lcd_bl_pwm_bsp.h"
#include "hud_cams.h"
#include "hud_anim.h"
#include "cam_alert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

_Static_assert(SIM_W == HUD_PANEL_W && SIM_H == HUD_PANEL_H, "SIM_W/SIM_H должны совпадать с HUD_PANEL_W/H из user_config.h");

/* ============================ время ============================ */
/* s_sim — «время HUD», мс: его видят декодер и экран через esp_timer_get_time().
   Идёт со скоростью проигрывания; на паузе стоит; при перемотке и смене поездки прыгает
   вперёд на SEEK_GAP_MS (больше любого таймаута свежести) — так состояние HUD сбрасывается
   без единой правки в can_decode.c. Назад это время не идёт никогда. */
#define SEEK_GAP_MS   120000
#define PREROLL_MS    30000
static double   s_sim = 10000.0;
static int64_t  s_clock_ms = 10000;      /* во время подачи кадра — его метка времени */
static uint32_t s_lv_tick;               /* реальное время для LVGL (анимации виджетов, таймеры) */

int64_t  esp_timer_get_time(void) { return s_clock_ms * 1000; }
uint32_t sim_lv_tick(void)        { return s_lv_tick; }

/* ============================ платформа: два декодера ============================ */
/* can_decode.c собран дважды (sim_decode_mlb.c / sim_decode_mqb.c); здесь — функции с именами прошивки,
   которые зовут выбранный экземпляр. У каждого своё состояние; psd_speedlimit.c общий. */
#define DECL(p) \
    uint32_t p##_hud_now_ms(void); void p##_hud_data_snapshot(HudData *out); void p##_hud_data_set_link(bool up); \
    void p##_hud_data_note_lost(void); uint32_t p##_hud_data_take_speed_gap_max(void); \
    void p##_hud_data_debug_print(void (*out)(const char *line)); void p##_hud_data_fake_doors(uint8_t mask); \
    void p##_can_decode_frame(uint32_t id, bool ext, const uint8_t *data, uint8_t dlc);
DECL(mlb) DECL(mqb)
#undef DECL
static int s_plat = SIM_PLATFORM_MLB_EVO;
#define MQB (s_plat == SIM_PLATFORM_MQB)

uint32_t hud_now_ms(void) { return (uint32_t)s_clock_ms; }
void hud_data_snapshot(HudData *out) { if (MQB) mqb_hud_data_snapshot(out); else mlb_hud_data_snapshot(out); }
void hud_data_set_link(bool up) { mlb_hud_data_set_link(up); mqb_hud_data_set_link(up); }
void hud_data_note_lost(void) { if (MQB) mqb_hud_data_note_lost(); else mlb_hud_data_note_lost(); }
uint32_t hud_data_take_speed_gap_max(void) { return MQB ? mqb_hud_data_take_speed_gap_max() : mlb_hud_data_take_speed_gap_max(); }
void hud_data_debug_print(void (*out)(const char *line)) { if (MQB) mqb_hud_data_debug_print(out); else mlb_hud_data_debug_print(out); }
void hud_data_fake_doors(uint8_t mask) { if (MQB) mqb_hud_data_fake_doors(mask); else mlb_hud_data_fake_doors(mask); }
void can_decode_frame(uint32_t id, bool ext, const uint8_t *data, uint8_t dlc)
{
    if (MQB) mqb_can_decode_frame(id, ext, data, dlc); else mlb_can_decode_frame(id, ext, data, dlc);
}
int sim_platform(void) { return s_plat; }

/* ============================ экран ============================ */
uint32_t sim_fb[SIM_W * SIM_H];
volatile bool sim_fb_dirty;
int sim_backlight = -1;

static uint32_t s_refr_n, s_refr_px;
void sim_refresh_stats(uint32_t *refreshes, uint32_t *pixels, uint32_t *lv_ms)
{
    if (refreshes) *refreshes = s_refr_n;
    if (pixels) *pixels = s_refr_px;
    if (lv_ms) *lv_ms = s_lv_tick;
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px)
{
    s_refr_px += (uint32_t)(a->x2 - a->x1 + 1) * (uint32_t)(a->y2 - a->y1 + 1);
    if (lv_disp_flush_is_last(drv)) s_refr_n++;
    for (int y = a->y1; y <= a->y2; y++) {
        uint32_t *dst = sim_fb + y * SIM_W + a->x1;
        for (int x = a->x1; x <= a->x2; x++, px++) *dst++ = lv_color_to32(*px) & 0x00FFFFFFu;
    }
    sim_fb_dirty = true;
    lv_disp_flush_ready(drv);
}

/* ============================ заглушки железа ============================ */
void hud_log_write(const char *s) { sim_plat_log(s); }
void arduino_printf(const char *format, ...)
{
    char b[512];
    va_list ap; va_start(ap, format); vsnprintf(b, sizeof b, format, ap); va_end(ap);
    sim_plat_log(b);
}

void lcd_bl_pwm_bsp_init(uint16_t duty) { sim_backlight = 255 - (int)duty; }
void setUpduty(uint16_t duty)           { sim_backlight = 255 - (int)duty; }

/* ---- звук: те же тоновые последовательности, что в hud_sound.cpp (меандр на пассивный динамик) ----
   Таблицы повторяют прошивку; громкость на ПК условная (амплитуда пропорциональна проценту). */
typedef struct { uint16_t hz, ms; } snd_note_t;
static const snd_note_t k_beep[]   = {{2400, 80}};
static const snd_note_t k_double[] = {{2400, 70}, {0, 70}, {2400, 70}};
static const snd_note_t k_rising[] = {{1500, 80}, {0, 20}, {2000, 80}, {0, 20}, {2600, 120}};
static const snd_note_t k_warn[]   = {{3000, 110}, {0, 70}, {3000, 110}, {0, 70}, {3000, 110}};
static const snd_note_t k_cam[]    = {{300, 100}};
static const struct { const snd_note_t *n; int len; } k_pat[HUD_SND_COUNT] = {
    { k_beep, 1 }, { k_double, 3 }, { k_rising, 5 }, { k_warn, 5 }, { k_cam, 1 } };

static bool    s_snd_on = true;
static uint8_t s_vol = (uint8_t)((HUD_SOUND_DUTY_PCT > 50 ? 50 : HUD_SOUND_DUTY_PCT) * 2);
void    hud_sound_begin(void) {}
void    hud_sound_set_enabled(bool on) { s_snd_on = on; }
bool    hud_sound_enabled(void) { return s_snd_on; }
void    hud_sound_set_volume(uint8_t pct) { s_vol = pct > 100 ? 100 : pct; }
uint8_t hud_sound_volume(void) { return s_vol; }
void    hud_sound_play(hud_sound_t id)
{
    enum { RATE = 22050 };
    if (!s_snd_on || !s_vol || (unsigned)id >= HUD_SND_COUNT) return;
    uint32_t total = 0;
    for (int i = 0; i < k_pat[id].len; i++) total += (uint32_t)k_pat[id].n[i].ms * RATE / 1000;
    int16_t *pcm = (int16_t *)malloc((size_t)total * 2);
    if (!pcm) return;
    uint32_t o = 0;
    for (int i = 0; i < k_pat[id].len; i++) {
        uint32_t n = (uint32_t)k_pat[id].n[i].ms * RATE / 1000, hz = k_pat[id].n[i].hz;
        for (uint32_t k = 0; k < n; k++, o++)
            pcm[o] = hz ? (int16_t)((((uint64_t)k * hz * 2 / RATE) & 1) ? -9000 : 9000) : 0;
    }
    sim_plat_play(pcm, total, RATE, s_vol);
    free(pcm);
}

/* фоторезистор: на ПК нет — напряжение на нём задаёт ползунок в окне настроек */
static bool s_ldr_on = HUD_LDR_ON;
static int  s_ldr_mv = 1200;
static int  s_ldr_dark = HUD_LDR_DARK_MV, s_ldr_bright = HUD_LDR_BRIGHT_MV;
void hud_light_poll(void) {}
void hud_light_force(bool on) { (void)on; }
void hud_light_sample_now(void) {}
int  hud_light_mv(void) { return s_ldr_on ? s_ldr_mv : -1; }
bool hud_light_enabled(void) { return s_ldr_on; }
void hud_light_set_enabled(bool on) { s_ldr_on = on; }
void hud_light_get_cal(int *d, int *b) { if (d) *d = s_ldr_dark; if (b) *b = s_ldr_bright; }
void hud_light_set_cal(int d, int b) { s_ldr_dark = d; s_ldr_bright = b; }
bool hud_light_cal_valid(int d, int b) { return abs(b - d) >= HUD_LDR_MIN_SPAN_MV; }
int  hud_light_lux(void)
{
    if (!s_ldr_on || !hud_light_cal_valid(s_ldr_dark, s_ldr_bright)) return -1;
    float t = (s_ldr_mv - s_ldr_dark) / (float)(s_ldr_bright - s_ldr_dark);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float lo = log10f(HUD_LIGHT_LUX_DARK + 10.0f), hi = log10f(HUD_LIGHT_LUX_BRIGHT + 10.0f);
    return (int)(powf(10.0f, lo + t * (hi - lo)) - 10.0f + 0.5f);
}

/* источник кадров — файл лога; для HUD это «свой трансивер» (CAN) */
static bool s_loaded;
void hud_source_start(void) {}
void hud_src_set_up(int src, bool up) { (void)src; (void)up; }
void hud_src_twai_frame(void) {}
bool hud_src_twai_active(void) { return s_loaded; }
int  hud_src_current(void) { return s_loaded ? 1 : -1; }
void hud_src_set_pair_failed(bool on) { (void)on; }
bool hud_src_pair_failed(void) { return false; }

/* база камер */
static bool s_cams_ready;
void hud_cams_start(void) {}
bool hud_cams_ready(void) { return s_cams_ready; }

/* arrows.bin: в прошивке лежит во флеше W25Q128, кадры читаются оттуда по одному; здесь «флешем» служит буфер в памяти.
   Без файла HUD рисует обычные стрелки с баром. */
static const uint8_t *s_arrows;
static size_t         s_arrows_len;
static bool arrows_rd(uint32_t addr, uint8_t *dst, size_t len)
{
    if (!s_arrows || (size_t)addr + len > s_arrows_len) return false;
    memcpy(dst, s_arrows + addr, len);
    return true;
}
bool sim_set_arrows(const void *buf, size_t len)
{
    hud_anim_close();
    s_arrows = (const uint8_t *)buf; s_arrows_len = len;
    return hud_anim_open(arrows_rd, 0, (uint32_t)len);
}

bool sim_set_cams(const void *buf, size_t len)
{
    cam_cfg_t c = { (float)HUD_CAM_AHEAD_S, (float)HUD_CAM_MIN_M, (float)HUD_CAM_MAX_M,
                    (float)HUD_CAM_CROSS_M, (float)HUD_CAM_DIR_DEG, (uint16_t)HUD_CAM_TYPE_MASK };
    cam_config(&c);
    s_cams_ready = cam_load(buf, len);
    return s_cams_ready;
}


/* ============================ настройки (вместо приложения на Android) ============================ */
/* В прошивке настройки приходят по BLE и применяются через hud_set_* (hud_ble_settings.cpp);
   здесь их задаёт окно настроек (sim_win.c) теми же вызовами. */
void sim_settings_defaults(sim_settings_t *c)
{
    c->lang = HUD_LANG; c->units = HUD_UNITS; c->psd = HUD_PSD_LIMITS; c->vze = HUD_VZE_SIGNS;
    c->gallons = HUD_VOLUME_GAL; c->tank_l = HUD_TANK_L; c->accel_bar = HUD_ACCEL_BAR;
    c->tol = HUD_OVERSPEED_TOL_KMH; c->cams = HUD_CAMS_ON; c->cam_beep = HUD_CAM_BEEP;
    c->volume = (uint8_t)((HUD_SOUND_DUTY_PCT > 50 ? 50 : HUD_SOUND_DUTY_PCT) * 2);
    c->ldr_on = HUD_LDR_ON; c->ldr_mv = 1200;
}

void sim_settings_apply(const sim_settings_t *c)
{
    hud_set_lang(c->lang); hud_set_units(c->units);
    hud_set_psd(c->psd != 0); hud_set_vze(c->vze != 0);
    hud_set_gallons(c->gallons != 0); hud_set_tank_l(c->tank_l);
    hud_set_accel_bar(c->accel_bar != 0); hud_set_overspeed_tol(c->tol);
    hud_set_cams(c->cams != 0); hud_set_cam_beep(c->cam_beep != 0);
    hud_sound_set_volume(c->volume);
    s_ldr_on = c->ldr_on != 0; s_ldr_mv = c->ldr_mv;
}

void sim_settings_read(sim_settings_t *c)       /* что сейчас в HUD */
{
    c->lang = hud_get_lang(); c->units = hud_get_units();
    c->psd = hud_get_psd(); c->vze = hud_get_vze();
    c->gallons = hud_get_gallons(); c->tank_l = hud_get_tank_l();
    c->accel_bar = hud_get_accel_bar(); c->tol = hud_get_overspeed_tol();
    c->cams = hud_get_cams(); c->cam_beep = hud_get_cam_beep();
    c->volume = hud_sound_volume();
    c->ldr_on = s_ldr_on; c->ldr_mv = s_ldr_mv;
}

/* ============================ проигрыватель ============================ */
typedef struct { uint32_t ts; uint32_t id; uint8_t dlc; uint8_t d[8]; } frame_t;   /* id: бит 31 — 29-битный */

static frame_t  *s_fr;
static uint32_t  s_n, s_pos;
static int64_t   s_map;                  /* время HUD = s_map + ts */
static uint32_t  s_last_ts;              /* последняя метка предыдущего файла (исходная, до выравнивания) */
static uint32_t  s_prev_adj;
static bool      s_eof_sent;
static bool      s_have_prev;
static bool      s_playing, s_eof;
static double    s_speed = 1.0;

static sim_man_t  *s_man;  static uint32_t s_man_n, s_man_cap;
static sim_span_t *s_span; static uint32_t s_span_n, s_span_cap;

static void man_add(uint32_t t, uint8_t main, uint8_t dir)
{
    if (s_man_n && s_man[s_man_n - 1].main == main && s_man[s_man_n - 1].dir == dir) return;
    if (s_man_n == s_man_cap) { s_man_cap = s_man_cap ? s_man_cap * 2 : 256; s_man = (sim_man_t *)realloc(s_man, s_man_cap * sizeof *s_man); }
    s_man[s_man_n].t = t; s_man[s_man_n].main = main; s_man[s_man_n].dir = dir; s_man_n++;
}

static void span_set(uint32_t t, bool active)
{
    bool open = s_span_n && s_span[s_span_n - 1].t1 == 0xFFFFFFFFu;
    if (active && !open) {
        if (s_span_n == s_span_cap) { s_span_cap = s_span_cap ? s_span_cap * 2 : 64; s_span = (sim_span_t *)realloc(s_span, s_span_cap * sizeof *s_span); }
        s_span[s_span_n].t0 = t; s_span[s_span_n].t1 = 0xFFFFFFFFu; s_span_n++;
    } else if (!active && open) s_span[s_span_n - 1].t1 = t;
}

/* метки для шкалы: RG_Status (0x11) и первые байты ManeuverDescriptor (0x17) из BAP Navigation_SD */
static void scan_nav(const frame_t *f, uint32_t t)
{
    if (f->id != (0x17333210u | 0x80000000u) || f->dlc < 3) return;
    const uint8_t *d = f->d;
    uint16_t hdr; const uint8_t *p; int n;
    if (!(d[0] & 0x80))      { hdr = (uint16_t)((d[0] << 8) | d[1]); p = d + 2; n = f->dlc - 2; }
    else if (!(d[0] & 0x40)) { if (f->dlc < 5) return; hdr = (uint16_t)((d[2] << 8) | d[3]); p = d + 4; n = f->dlc - 4; }
    else return;
    uint8_t op = (hdr >> 12) & 7, lsg = (hdr >> 6) & 0x3F, fct = hdr & 0x3F;
    if ((op != 3 && op != 4) || lsg != 0x32) return;
    if (fct == 0x11 && n >= 1) span_set(t, p[0] == 1);
    else if (fct == 0x17 && n >= 2) man_add(t, p[0], p[1]);
}

static int hexv(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c |= 0x20;
    return (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
}

bool sim_load_log(const char *text, size_t len, bool cont)
{
    uint32_t cap = (uint32_t)(len / 28 + 16), n = 0;
    frame_t *fr = (frame_t *)malloc((size_t)cap * sizeof *fr);
    if (!fr) return false;
    const char *p = text, *end = text + len;
    uint32_t prev = 0, adj = 0, raw_first = 0, raw_last = 0;
    bool first = true;
    s_man_n = 0; s_span_n = 0;
    while (p < end) {
        const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
        const char *le = nl ? nl : end;
        const char *q = p;
        p = nl ? nl + 1 : end;
        while (q < le && (*q == ' ' || *q == '\t')) q++;
        if (q >= le || *q < '0' || *q > '9') continue;                       /* '#' — метки сниффера, пустые строки */
        uint32_t ts = 0;
        while (q < le && *q >= '0' && *q <= '9') ts = ts * 10 + (uint32_t)(*q++ - '0');
        while (q < le && *q == ' ') q++;
        if (q >= le || (*q != 'S' && *q != 'X')) continue;                   /* S — 11 бит, X — 29 бит; прочее (RTR и т.п.) пропускаем */
        bool ext = *q++ == 'X';
        while (q < le && *q == ' ') q++;
        uint32_t id = 0; int h, k = 0;
        while (q < le && (h = hexv(*q)) >= 0) { id = (id << 4) | (uint32_t)h; q++; k++; }
        if (!k) continue;
        while (q < le && *q == ' ') q++;
        uint32_t dlc = 0; k = 0;
        while (q < le && *q >= '0' && *q <= '9') { dlc = dlc * 10 + (uint32_t)(*q++ - '0'); k++; }
        if (!k || dlc > 8) continue;
        frame_t f; memset(&f, 0, sizeof f);
        uint32_t got = 0;
        while (got < dlc) {
            while (q < le && *q == ' ') q++;
            int a, b;
            if (q + 1 >= le || (a = hexv(q[0])) < 0 || (b = hexv(q[1])) < 0) break;
            f.d[got++] = (uint8_t)((a << 4) | b); q += 2;
        }
        if (got != dlc) continue;                                            /* оборванная строка */
        if (first) { raw_first = ts; }
        raw_last = ts;
        /* метки времени — мс от включения сниффера; делаем их неубывающими: мелкую перестановку строк
           выравниваем, а перезапуск сниффера посреди файла превращаем в паузу SEEK_GAP_MS */
        uint32_t t = ts + adj;
        if (!first && t < prev) {
            if (prev - t > 5000) { adj += prev - t + SEEK_GAP_MS; t = ts + adj; }
            else t = prev;
        }
        first = false; prev = t;
        f.ts = t; f.id = id | (ext ? 0x80000000u : 0); f.dlc = (uint8_t)dlc;
        if (n == cap) { cap += cap / 2 + 1024; frame_t *nf = (frame_t *)realloc(fr, (size_t)cap * sizeof *fr); if (!nf) { free(fr); return false; } fr = nf; }
        fr[n++] = f;
    }
    if (!n) { free(fr); return false; }

    for (uint32_t i = 0; i < n; i++) scan_nav(&fr[i], fr[i].ts - fr[0].ts);
    span_set(fr[n - 1].ts - fr[0].ts, false);

    /* продолжение той же поездки: метки времени идут дальше с того же места — шкала времени HUD не рвётся */
    int64_t map_join = s_map + (int64_t)s_prev_adj;
    bool joined = cont && s_loaded && s_have_prev && raw_first >= s_last_ts && raw_first - s_last_ts < 10000
                  && s_sim <= (double)(map_join + (int64_t)fr[0].ts) + 2000.0;
    free(s_fr);
    s_fr = fr; s_n = n; s_pos = 0;
    if (joined) s_map = map_join;
    else {
        s_map = (int64_t)s_sim + SEEK_GAP_MS - (int64_t)fr[0].ts;
        s_sim = (double)(s_map + (int64_t)fr[0].ts);
    }
    s_clock_ms = (int64_t)s_sim;
    s_last_ts = raw_last; s_prev_adj = adj; s_have_prev = true;
    s_loaded = true; s_eof = false;
    hud_data_set_link(true);
    return true;
}

bool   sim_loaded(void)  { return s_loaded; }
void   sim_play(bool on) { s_playing = on && s_loaded; if (s_playing && s_pos >= s_n) s_eof = true; }
bool   sim_playing(void) { return s_playing; }
void   sim_set_speed(double k) { s_speed = k < 0.05 ? 0.05 : k > 64 ? 64 : k; }
double sim_speed(void)   { return s_speed; }
uint32_t sim_dur_ms(void) { return s_loaded ? s_fr[s_n - 1].ts - s_fr[0].ts : 0; }
uint32_t sim_frames_total(void) { return s_n; }
uint32_t sim_frames_pos(void)   { return s_pos; }
bool   sim_take_eof(void) { bool r = s_eof; s_eof = false; return r; }

uint32_t sim_pos_ms(void)
{
    if (!s_loaded) return 0;
    double p = s_sim - (double)s_map - (double)s_fr[0].ts;
    double d = (double)sim_dur_ms();
    return (uint32_t)(p < 0 ? 0 : p > d ? d : p);
}

const sim_man_t  *sim_maneuvers(uint32_t *count) { *count = s_man_n;  return s_man;  }
const sim_span_t *sim_nav_spans(uint32_t *count) { *count = s_span_n; return s_span; }

static void feed(const frame_t *f)
{
    s_clock_ms = s_map + (int64_t)f->ts;
    can_decode_frame(f->id & 0x1FFFFFFFu, (f->id & 0x80000000u) != 0, f->d, f->dlc);
}

static uint32_t lower_bound(uint32_t ts)
{
    uint32_t lo = 0, hi = s_n;
    while (lo < hi) { uint32_t m = lo + (hi - lo) / 2; if (s_fr[m].ts < ts) lo = m + 1; else hi = m; }
    return lo;
}

void sim_seek_ms(uint32_t pos_ms)
{
    if (!s_loaded) return;
    uint32_t dur = sim_dur_ms();
    if (pos_ms > dur) pos_ms = dur;
    uint32_t target = s_fr[0].ts + pos_ms;
    uint32_t from   = pos_ms > PREROLL_MS ? target - PREROLL_MS : s_fr[0].ts;
    uint32_t i0 = lower_bound(from), i1 = lower_bound(target);
    /* прыжок времени HUD вперёд: всё старое устаревает по таймаутам; затем «разгон» — последние
       30 с до точки подаются разом (навигация BAP приходит по изменению и раз в ~25 с) */
    s_map = (int64_t)s_sim + SEEK_GAP_MS - (int64_t)from;
    for (uint32_t i = i0; i < i1; i++) feed(&s_fr[i]);
    s_pos = i1;
    s_sim = (double)(s_map + (int64_t)target);
    s_clock_ms = (int64_t)s_sim;
    s_eof = false;
}

void sim_set_platform(int p)
{
    p = p == SIM_PLATFORM_MQB ? SIM_PLATFORM_MQB : SIM_PLATFORM_MLB_EVO;
    if (p == s_plat) return;
    s_plat = p;
    hud_set_splash_plat(p == SIM_PLATFORM_MQB ? "MQB" : "MLB EVO");
    /* у нового декодера состояние пустое или старое: та же точка лога с «разгоном» — уже через него */
    if (s_loaded) sim_seek_ms(sim_pos_ms());
}

/* ============================ запуск и шаг ============================ */
static void lv_log_cb(const char *buf) { if (HUD_LOG_LVGL) { sim_plat_log("[LVGL] "); sim_plat_log(buf); } }

void sim_init(void)
{
    static lv_disp_draw_buf_t draw;
    static lv_disp_drv_t      disp;
    static lv_color_t         buf1[SIM_W * HUD_STRIPE_H];

    lv_init();
    lv_log_register_print_cb(lv_log_cb);
    lv_disp_draw_buf_init(&draw, buf1, NULL, SIM_W * HUD_STRIPE_H);
    lv_disp_drv_init(&disp);
    disp.hor_res = SIM_W; disp.ver_res = SIM_H;
    disp.flush_cb = flush_cb;
    disp.draw_buf = &draw;
    disp.full_refresh = 0;                       /* частичная отрисовка полосами — как в прошивке (lvgl_port.cpp) */
    lv_disp_drv_register(&disp);
    hud_data_set_link(false);
    build_hud_mockup();
    lcd_bl_pwm_bsp_init(0);
}

void sim_step(uint32_t dt)
{
    s_lv_tick += dt;
    if (s_loaded && s_playing) {
        s_sim += dt * s_speed;
        while (s_pos < s_n && (double)(s_map + (int64_t)s_fr[s_pos].ts) <= s_sim) feed(&s_fr[s_pos++]);
        if (s_pos >= s_n && !s_eof_sent) { s_eof = true; s_eof_sent = true; }
        if (s_pos < s_n) s_eof_sent = false;
    } else if (!s_loaded) {
        s_sim += dt;                             /* лога нет — HUD живёт в реальном времени (меню, «нет связи») */
    }
    s_clock_ms = (int64_t)s_sim;
    lv_timer_handler();
}
