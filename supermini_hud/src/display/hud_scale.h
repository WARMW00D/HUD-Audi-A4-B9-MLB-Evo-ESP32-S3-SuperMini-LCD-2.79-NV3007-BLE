#ifndef HUD_SCALE_H
#define HUD_SCALE_H
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* RGB565 in native byte order. Destination is one contiguous output row. */
void hud_scale_row(const uint16_t *src, int sw, int sh,
                   uint16_t *dst, int dw, int dh, int y);
/* Accept LVGL byte-swapped input without modifying its framebuffer.
   Output is always native RGB565 for Arduino_GFX. */
void hud_scale_row_ordered(const uint16_t *src, int sw, int sh,
                           uint16_t *dst, int dw, int dh, int y, bool swapped);
#ifdef __cplusplus
}
#endif
#endif
