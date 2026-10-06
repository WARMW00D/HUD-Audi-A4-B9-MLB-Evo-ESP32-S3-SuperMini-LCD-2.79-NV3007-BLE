#ifndef HUD_DIRTY_H
#define HUD_DIRTY_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct { int x, y, w, h; } hud_dirty_rect_t;
/* Compare a scaled stripe with its last displayed pixels. Update the cache
   before packing the changed rectangle tightly at the start of stripe.
   cache and stripe contain width*rows native RGB565 pixels, without overlap.
   force sends the entire stripe without reading the uninitialized cache. */
bool hud_dirty_pack(uint16_t *stripe, uint16_t *cache, int width, int rows,
                    bool force, hud_dirty_rect_t *rect);
#ifdef __cplusplus
}
#endif
#endif
