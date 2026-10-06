#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hud_scale.h"

int main(void) {
    uint16_t a[518 * 172], row[428];
    uint16_t colors[] = {0, 0xffff, 0xf800, 0x07e0, 0x001f};
    for (unsigned k = 0; k < sizeof(colors)/sizeof(*colors); ++k) {
        for (int i = 0; i < 518 * 172; ++i) a[i] = colors[k];
        for (int y = 0; y < 142; ++y) {
            hud_scale_row(a, 518, 172, row, 428, 142, y);
            for (int x = 0; x < 428; ++x) assert(row[x] == colors[k]);
        }
    }
    for (int y = 0; y < 172; ++y)
        for (int x = 0; x < 518; ++x) a[y*518+x] = (uint16_t)(x + y * 518);
    uint16_t identity[518];
    for (int y = 0; y < 172; ++y) {
        hud_scale_row(a, 518, 172, identity, 518, 172, y);
        assert(!memcmp(identity, a + y * 518, sizeof(identity)));
    }
    uint16_t single = 0xf81f;
    hud_scale_row(&single, 1, 1, row, 428, 142, 114);
    for (int x = 0; x < 428; ++x) assert(row[x] == single);
    for (int y = 0; y < 142; ++y) hud_scale_row(a, 518, 172, row, 428, 142, y);
    /* Both LVGL storage orders must yield identical scaled output.
       Distinct non-solid input also verifies swap BEFORE interpolation. */
    static uint16_t swapped[518 * 172], saved[518 * 172];
    uint16_t other[428];
    for (int i = 0; i < 518 * 172; ++i) {
        a[i] = (uint16_t)((i * 1103515245u + 12345u) >> 8);
        swapped[i] = (uint16_t)((a[i] >> 8) | (a[i] << 8));
    }
    memcpy(saved, swapped, sizeof(swapped));
    for (int y = 0; y < 142; ++y) {
        hud_scale_row_ordered(a, 518, 172, row, 428, 142, y, false);
        hud_scale_row_ordered(swapped, 518, 172, other, 428, 142, y, true);
        assert(!memcmp(row, other, sizeof(row)));
    }
    assert(!memcmp(saved, swapped, sizeof(swapped)));
    puts("PASS: both LVGL byte orders match; source buffer unchanged.");
    puts("PASS: solid RGB565, identity, one-pixel source, all 142 output rows.");
}
