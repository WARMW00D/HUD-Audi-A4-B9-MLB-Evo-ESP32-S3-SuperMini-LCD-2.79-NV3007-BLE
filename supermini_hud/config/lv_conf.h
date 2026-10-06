/* Copy beside the lvgl library directory, not inside the sketch.
   Arduino/libraries/lv_conf.h, Arduino/libraries/lvgl/... */
#ifndef LV_CONF_H
#define LV_CONF_H
#include <stdint.h>
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_MEM_SIZE (48U * 1024U)
#define LV_USE_FONT_COMPRESSED 1
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_USE_LINE 1
#define LV_USE_IMG 1
#define LV_USE_LABEL 1
#endif
