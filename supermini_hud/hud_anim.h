/*
  hud_anim.h — анимированные стрелки навигации из файла arrows.bin (формат ANM1, tools/gen_arrow_anim.py)
  ------------------------------------------------------------------------------
  Файл лежит во флеш-памяти W25Q128 с адреса HUD_ANIM_FLASH_ADDR (после базы камер) и заливается с телефона
  тем же путём, что и cams.bin. В ОЗУ держатся только заголовок и таблицы (около 8 КБ); сам кадр
  (независимый блок LZ4, 2-4 КБ) читается из флеши прямо перед распаковкой и распаковывается в буфер
  4-битной картинки с палитрой (LV_IMG_CF_INDEXED_4BIT). Нет файла — hud_anim_ready() возвращает false,
  и HUD рисует обычные стрелки с баром.
*/
#ifndef HUD_ANIM_H
#define HUD_ANIM_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* чтение len байт файла с флеши (addr — абсолютный адрес); false — не удалось (например, шина занята) */
typedef bool (*hud_anim_read_fn)(uint32_t addr, uint8_t *dst, size_t len);

/* открыть файл, лежащий по адресу base (максимум max_len байт): прочитать заголовок и таблицы.
   false — формата ANM1 нет или он неверный (анимации отключены). */
bool hud_anim_open(hud_anim_read_fn rd, uint32_t base, uint32_t max_len);
void hud_anim_close(void);                            /* отключить анимации (буферы остаются для повторного open) */
bool hud_anim_ready(void);
uint32_t hud_anim_size(void);                         /* размер открытого файла, байт */
uint16_t hud_anim_width(void);
uint16_t hud_anim_height(void);

int  hud_anim_find(const char *name);                 /* номер анимации или -1 */
int  hud_anim_frames(int anim);                       /* число кадров */
/* прочитать и распаковать кадр; NULL при ошибке. Возвращённая картинка действует до двух следующих вызовов */
const lv_img_dsc_t *hud_anim_frame(int anim, int frame);

/* имя анимации по коду манёвра (MainElement) и направлению; false — для этого манёвра анимации нет */
bool hud_anim_name(char *out, size_t n, uint8_t main, uint8_t dir, bool left_hand_traffic);

#ifdef __cplusplus
}
#endif
#endif
