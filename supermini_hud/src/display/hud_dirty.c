#include "hud_dirty.h"
#include <string.h>

bool hud_dirty_pack(uint16_t *stripe, uint16_t *cache, int width, int rows,
                    bool force, hud_dirty_rect_t *rect)
{
    int x0 = width, x1 = -1, y0 = rows, y1 = -1;
    if (force) {
        x0 = 0; x1 = width - 1; y0 = 0; y1 = rows - 1;
    } else {
        for (int y = 0; y < rows; ++y) {
            const uint16_t *current = stripe + y * width;
            const uint16_t *previous = cache + y * width;
            if (memcmp(current, previous, (size_t)width * sizeof(uint16_t)) == 0)
                continue;
            if (y0 > y) y0 = y;
            y1 = y;
            for (int x = 0; x < width; ++x) {
                if (current[x] == previous[x]) continue;
                if (x0 > x) x0 = x;
                if (x1 < x) x1 = x;
            }
        }
    }
    if (x1 < x0) return false;
    /* Save the full stripe before in-place compaction changes its row stride. */
    memcpy(cache, stripe, (size_t)width * rows * sizeof(uint16_t));
    rect->x = x0; rect->y = y0;
    rect->w = x1 - x0 + 1; rect->h = y1 - y0 + 1;
    for (int y = 0; y < rect->h; ++y)
        memmove(stripe + y * rect->w,
                stripe + (y0 + y) * width + x0,
                (size_t)rect->w * sizeof(uint16_t));
    return true;
}
