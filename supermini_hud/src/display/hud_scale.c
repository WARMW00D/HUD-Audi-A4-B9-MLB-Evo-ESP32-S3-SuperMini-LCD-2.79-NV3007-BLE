#include "hud_scale.h"

/* Pixel-center mapping and bilinear interpolation, 8-bit fractional weights.
   Clamped edges preserve the outermost row/column without out-of-bounds reads. */
static int coord(int p, int source, int target)
{
    int v = (int)(((int64_t)(2 * p + 1) * source * 128) / target) - 128;
    if (v < 0) v = 0;
    if (v > (source - 1) * 256) v = (source - 1) * 256;
    return v;
}

static unsigned channel(uint16_t a, uint16_t b, uint16_t c, uint16_t d,
                        int shift, unsigned mask, unsigned fx, unsigned fy)
{
    unsigned top = ((a >> shift) & mask) * (256 - fx) + ((b >> shift) & mask) * fx;
    unsigned bot = ((c >> shift) & mask) * (256 - fx) + ((d >> shift) & mask) * fx;
    return (top * (256 - fy) + bot * fy + 32768) >> 16;
}

static uint16_t native_color(uint16_t color, bool swapped)
{
    return swapped ? (uint16_t)((color >> 8) | (color << 8)) : color;
}

void hud_scale_row_ordered(const uint16_t *src, int sw, int sh,
                           uint16_t *dst, int dw, int dh, int y, bool swapped)
{
    int sy = coord(y, sh, dh), y0 = sy >> 8, y1 = y0 + 1 < sh ? y0 + 1 : y0;
    unsigned fy = sy & 255;
    for (int x = 0; x < dw; ++x) {
        int sx = coord(x, sw, dw), x0 = sx >> 8, x1 = x0 + 1 < sw ? x0 + 1 : x0;
        unsigned fx = sx & 255;
        uint16_t a = native_color(src[y0 * sw + x0], swapped);
        uint16_t b = native_color(src[y0 * sw + x1], swapped);
        uint16_t c = native_color(src[y1 * sw + x0], swapped);
        uint16_t d = native_color(src[y1 * sw + x1], swapped);
        dst[x] = (uint16_t)((channel(a,b,c,d,11,31,fx,fy) << 11) |
                           (channel(a,b,c,d,5,63,fx,fy) << 5) |
                            channel(a,b,c,d,0,31,fx,fy));
    }
}

void hud_scale_row(const uint16_t *src, int sw, int sh,
                   uint16_t *dst, int dw, int dh, int y)
{
    hud_scale_row_ordered(src, sw, sh, dst, dw, dh, y, false);
}
