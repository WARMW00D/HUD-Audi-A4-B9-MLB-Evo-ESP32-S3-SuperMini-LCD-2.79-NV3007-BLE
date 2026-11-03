/*
  hud_anim.c — см. hud_anim.h. Формат файла описан в tools/gen_arrow_anim.py.
*/
#include "hud_anim.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#define ANIM_ALLOC(n)  heap_caps_malloc((n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define ANIM_ALLOC_I(n) heap_caps_malloc((n), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#define ANIM_FREE(p)   heap_caps_free(p)
#else
#define ANIM_ALLOC(n)  malloc(n)
#define ANIM_ALLOC_I(n) malloc(n)
#define ANIM_FREE(p)   free(p)
#endif

#define PAL_BYTES 64                     /* 16 цветов x 4 байта */
#define HDR_BYTES 20

static volatile bool  s_ready;
static hud_anim_read_fn s_rd;
static uint32_t       s_base, s_size;
static uint16_t       s_w, s_h, s_nanim;
static uint32_t       s_nframes;
static uint8_t       *s_tab;             /* копия «заголовок + палитра + таблица анимаций + таблица смещений» */
static size_t         s_tab_cap;
static const uint8_t *s_pal, *s_anim, *s_offs;
static size_t         s_raw;             /* размер распакованного кадра, байт */
static uint8_t       *s_dec[2];          /* [палитра][пиксели], две штуки по очереди — чтобы LVGL не читал буфер, который пишем */
static size_t         s_dec_cap;
static lv_img_dsc_t   s_dsc[2];
static uint8_t       *s_blk;             /* сжатый блок кадра */
static size_t         s_blk_cap;
static int            s_cur;

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* блок LZ4 (без заголовка кадра) -> dst; возвращает длину или -1 */
static int lz4_dec(const uint8_t *src, int slen, uint8_t *dst, int dcap)
{
    const uint8_t *ip = src, *ie = src + slen;
    uint8_t *op = dst, *oe = dst + dcap;
    while (ip < ie) {
        unsigned tok = *ip++;
        unsigned ll = tok >> 4;
        if (ll == 15) { unsigned s; do { if (ip >= ie) return -1; s = *ip++; ll += s; } while (s == 255); }
        if (ll > (unsigned)(ie - ip) || ll > (unsigned)(oe - op)) return -1;
        memcpy(op, ip, ll); op += ll; ip += ll;
        if (ip >= ie) break;                                   /* последняя последовательность — только литералы */
        if (ie - ip < 2) return -1;
        unsigned off = (unsigned)ip[0] | ((unsigned)ip[1] << 8); ip += 2;
        if (off == 0 || off > (unsigned)(op - dst)) return -1;
        unsigned ml = tok & 15;
        if (ml == 15) { unsigned s; do { if (ip >= ie) return -1; s = *ip++; ml += s; } while (s == 255); }
        ml += 4;
        if (ml > (unsigned)(oe - op)) return -1;
        const uint8_t *m = op - off;
        while (ml--) *op++ = *m++;                             /* копия с перекрытием — побайтно */
    }
    return (int)(op - dst);
}

bool     hud_anim_ready(void)  { return s_ready; }
uint32_t hud_anim_size(void)   { return s_ready ? s_size : 0; }
uint16_t hud_anim_width(void)  { return s_ready ? s_w : 0; }
uint16_t hud_anim_height(void) { return s_ready ? s_h : 0; }
void     hud_anim_close(void)  { s_ready = false; }

bool hud_anim_open(hud_anim_read_fn rd, uint32_t base, uint32_t max_len)
{
    s_ready = false;
    if (!rd) return false;
    uint8_t h[HDR_BYTES];
    if (!rd(base, h, sizeof h) || memcmp(h, "ANM1", 4) != 0) return false;
    uint16_t w = rd16(h + 4), ht = rd16(h + 6), n = rd16(h + 8);
    uint32_t size = rd32(h + 12);
    if (w == 0 || ht == 0 || (w & 1) || n == 0 || n > 200 || size < HDR_BYTES + PAL_BYTES || size > max_len) return false;
    /* число кадров — по таблице анимаций */
    size_t anim_at = HDR_BYTES + PAL_BYTES, offs_at = anim_at + (size_t)n * 32;
    if (offs_at > size) return false;
    uint8_t *ta = (uint8_t *)ANIM_ALLOC_I(offs_at);
    if (!ta) return false;
    if (!rd(base, ta, offs_at)) { ANIM_FREE(ta); return false; }
    uint32_t total = 0;
    for (int i = 0; i < n; i++) {
        uint32_t last = rd32(ta + anim_at + i * 32 + 28) + rd16(ta + anim_at + i * 32 + 24);
        if (last > total) total = last;
    }
    size_t tab_len = offs_at + (size_t)total * 4;
    if (total == 0 || tab_len > size || total > 20000) { ANIM_FREE(ta); return false; }
    ANIM_FREE(ta);
    if (s_tab_cap < tab_len) {
        if (s_tab) ANIM_FREE(s_tab);
        s_tab = (uint8_t *)ANIM_ALLOC_I(tab_len); s_tab_cap = s_tab ? tab_len : 0;
        if (!s_tab) return false;
    }
    if (!rd(base, s_tab, tab_len)) return false;
    size_t raw = (size_t)((w + 1) / 2) * ht;
    if (s_dec_cap < PAL_BYTES + raw) {
        for (int i = 0; i < 2; i++) { if (s_dec[i]) { ANIM_FREE(s_dec[i]); s_dec[i] = NULL; } }
        for (int i = 0; i < 2; i++) { s_dec[i] = (uint8_t *)ANIM_ALLOC(PAL_BYTES + raw); if (!s_dec[i]) { s_dec_cap = 0; return false; } }
        s_dec_cap = PAL_BYTES + raw;
    }
    size_t blk = raw + raw / 255 + 32;                          /* худший случай LZ4 */
    if (s_blk_cap < blk) {
        if (s_blk) ANIM_FREE(s_blk);
        s_blk = (uint8_t *)ANIM_ALLOC(blk); s_blk_cap = s_blk ? blk : 0;
        if (!s_blk) return false;
    }
    for (int i = 0; i < 2; i++) {
        memcpy(s_dec[i], s_tab + HDR_BYTES, PAL_BYTES);
        memset(&s_dsc[i], 0, sizeof s_dsc[i]);
        s_dsc[i].header.cf = LV_IMG_CF_INDEXED_4BIT;
        s_dsc[i].header.w = w; s_dsc[i].header.h = ht;
        s_dsc[i].data_size = (uint32_t)(PAL_BYTES + raw);
        s_dsc[i].data = s_dec[i];
    }
    s_rd = rd; s_base = base; s_size = size; s_w = w; s_h = ht; s_nanim = n; s_nframes = total;
    s_pal = s_tab + HDR_BYTES; s_anim = s_tab + anim_at; s_offs = s_tab + offs_at; s_raw = raw;
    s_cur = 0;
    s_ready = true;
    return true;
}

int hud_anim_find(const char *name)
{
    if (!s_ready || !name) return -1;
    for (int i = 0; i < s_nanim; i++)
        if (strncmp((const char *)s_anim + i * 32, name, 24) == 0) return i;
    return -1;
}

int hud_anim_frames(int anim)
{
    if (!s_ready || anim < 0 || anim >= s_nanim) return 0;
    return rd16(s_anim + anim * 32 + 24);
}

const lv_img_dsc_t *hud_anim_frame(int anim, int frame)
{
    if (!s_ready) return NULL;
    int n = hud_anim_frames(anim);
    if (n == 0) return NULL;
    if (frame < 0) frame = 0;
    if (frame >= n) frame = n - 1;
    uint32_t idx = rd32(s_anim + anim * 32 + 28) + (uint32_t)frame;
    if (idx >= s_nframes) return NULL;
    uint32_t off = rd32(s_offs + (size_t)idx * 4);
    uint8_t lenb[2];
    if (off + 2 > s_size || !s_rd(s_base + off, lenb, 2)) return NULL;
    uint16_t clen = rd16(lenb);
    if (clen == 0 || off + 2 + clen > s_size || clen > s_blk_cap) return NULL;
    if (!s_rd(s_base + off + 2, s_blk, clen)) return NULL;
    int k = s_cur ^ 1;
    if (lz4_dec(s_blk, clen, s_dec[k] + PAL_BYTES, (int)s_raw) != (int)s_raw) return NULL;
    s_cur = k;
    return &s_dsc[k];
}

/* Имя анимации в arrows.bin (tools/gen_arrow_anim.py) по коду манёвра и направлению. */
bool hud_anim_name(char *out, size_t n, uint8_t main, uint8_t dir, bool lht)
{
    bool right = dir > 0x80;
    int  deg   = (int)(((unsigned)dir * 360u + 128u) / 256u);         /* против часовой от «прямо», 0..360 */
    switch (main) {
    case 0x00: case 0x01: case 0x09: case 0x0A: return false;
    case 0x0B: snprintf(out, n, "approach"); return true;                           /* FollowStreet */
    case 0x03: snprintf(out, n, dir == 0 ? "arrived_c" : right ? "arrived_r" : "arrived_l"); return true;
    case 0x0F: snprintf(out, n, "exit_r"); return true;
    case 0x10: snprintf(out, n, "exit_l"); return true;
    case 0x13: case 0x14:
        if (dir == 0) right = (main == 0x13);
        snprintf(out, n, right ? "exit_r" : "exit_l");
        return true;
    case 0x19: snprintf(out, n, right ? "uturn_lht" : "uturn_rht"); return true;
    case 0x15: case 0x16: {                                                        /* кольцо: угол съезда по часовой, шаг 45° */
        int cw = (360 - deg) % 360;
        int e  = ((cw + 22) / 45) * 45;
        if (e == 0 || e >= 360) e = lht ? 45 : 315;                                /* «прямо» — ближайший к нему съезд */
        snprintf(out, n, "roundabout_%s_e%03d", lht ? "lht" : "rht", e);
        return true;
    }
    default: {                                                                     /* поворот (и неизвестные коды, как у обычных стрелок) */
        int mag = right ? 360 - deg : deg;                                         /* 0..180 */
        if (mag >= 172) { snprintf(out, n, right ? "uturn_lht" : "uturn_rht"); return true; }
        int st = ((mag + 7) / 15) * 15;
        if (st == 0) snprintf(out, n, "approach");                                 /* «прямо» — как FollowStreet */
        else         snprintf(out, n, right ? "turn_r%03d" : "turn_l%03d", st);
        return true;
    }
    }
}
