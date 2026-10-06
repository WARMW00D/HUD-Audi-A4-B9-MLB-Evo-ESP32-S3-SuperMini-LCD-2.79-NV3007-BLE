#ifndef HUD_OVERSPEED_H
#define HUD_OVERSPEED_H
/* Preserve the existing fade, with a defined zero-tolerance behaviour. */
static inline int hud_overspeed_opacity(float excess_kmh, unsigned tolerance_kmh, unsigned start_percent) {
    if (excess_kmh <= 0) return 0;
    if (tolerance_kmh == 0) return 255;
    if (start_percent >= 100) return excess_kmh >= tolerance_kmh ? 255 : 0;
    float start = start_percent / 100.0f;
    float fraction = (excess_kmh / tolerance_kmh - start) / (1.0f - start);
    if (fraction <= 0) return 0;
    if (fraction >= 1) return 255;
    return (int)(fraction * 255);
}
#endif
