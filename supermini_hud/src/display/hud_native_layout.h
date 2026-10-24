#ifndef HUD_NATIVE_LAYOUT_H
#define HUD_NATIVE_LAYOUT_H
#include "lvgl.h"
#include "../../user_config.h"
/* Convert authored positions on setters only. Pixels are never resized in flush.
   Signed rounding preserves negative offsets. */
static inline lv_coord_t hud_native_round(int value, int dst, int src) {
    return value < 0 ? -((-value * dst + src / 2) / src)
                     : (value * dst + src / 2) / src;
}
static inline lv_coord_t hud_native_x(int x) { return hud_native_round(x, HUD_PANEL_W, HUD_LAYOUT_W); }
static inline lv_coord_t hud_native_y(int y) { return hud_native_round(y, HUD_PANEL_H, HUD_LAYOUT_H); }
static inline lv_coord_t hud_native_metric(int n) {
    if(n == LV_RADIUS_CIRCLE) return LV_RADIUS_CIRCLE;
    return hud_native_x(n);
}
static inline void hud_native_set_pos(lv_obj_t *o, int x, int y) { lv_obj_set_pos(o, hud_native_x(x), hud_native_y(y)); }
static inline void hud_native_set_size(lv_obj_t *o, int w, int h) { lv_obj_set_size(o, hud_native_x(w), hud_native_y(h)); }
static inline void hud_native_set_width(lv_obj_t *o, int w) { lv_obj_set_width(o, hud_native_x(w)); }
static inline void hud_native_align(lv_obj_t *o, lv_align_t a, int x, int y) { lv_obj_align(o, a, hud_native_x(x), hud_native_y(y)); }
static inline int hud_layout_image_width(const lv_img_dsc_t *img) { return (img->header.w * HUD_LAYOUT_W + HUD_PANEL_W / 2) / HUD_PANEL_W; }
#endif
