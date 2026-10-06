#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "hud_dirty.h"

#define W 428
#define H 142
static uint16_t desired[W * H], displayed[W * H], cache[W * H];
static uint16_t stripe[W * 8];
static uint32_t rng = 7;
static unsigned next(void) { rng = rng * 1664525u + 1013904223u; return rng; }

static unsigned flush(bool force) {
    unsigned sent = 0;
    for (int y = 0; y < H; y += 8) {
        int rows = H - y < 8 ? H - y : 8;
        memcpy(stripe, desired + y * W, (size_t)rows * W * sizeof(uint16_t));
        hud_dirty_rect_t r;
        if (!hud_dirty_pack(stripe, cache + y * W, W, rows, force, &r)) continue;
        assert(r.x >= 0 && r.y >= 0 && r.w > 0 && r.h > 0);
        assert(r.x + r.w <= W && r.y + r.h <= rows);
        for (int row = 0; row < r.h; ++row)
            memcpy(displayed + (y + r.y + row) * W + r.x,
                   stripe + row * r.w, (size_t)r.w * sizeof(uint16_t));
        sent += r.w * r.h;
    }
    assert(memcmp(displayed, desired, sizeof(desired)) == 0);
    assert(memcmp(cache, desired, sizeof(desired)) == 0);
    return sent;
}

int main(void) {
    memset(cache, 0xA5, sizeof(cache));
    memset(displayed, 0xFF, sizeof(displayed));
    assert(flush(true) == W * H); /* force black first frame */
    assert(flush(false) == 0);
    desired[W * H - 1] = 0xF800;
    assert(flush(false) == 1); /* final partial stripe and bottom/right edge */
    desired[W * H - 1] = 0;
    assert(flush(false) == 1); /* erase old digit pixels */
    for (int i = 0; i < 100; ++i) {
        int x = next() % W, y = next() % H;
        int w = 1 + next() % (W - x), h = 1 + next() % (H - y);
        for (int row = y; row < y + h; ++row)
            for (int col = x; col < x + w; ++col)
                desired[row * W + col] = i % 3 ? (uint16_t)next() : 0;
        flush(false);
        assert(flush(false) == 0);
    }
    memset(desired, 0xFF, sizeof(desired));
    flush(false);
    memset(desired, 0, sizeof(desired));
    assert(flush(false) == W * H);
    puts("PASS: display replay matches every frame; unchanged stripes skipped, erasure, edge pixels, partial last stripe, random rectangles.");
}
