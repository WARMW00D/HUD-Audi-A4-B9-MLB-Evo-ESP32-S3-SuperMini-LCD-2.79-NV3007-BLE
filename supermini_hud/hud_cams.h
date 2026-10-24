/*
  hud_cams.h — база камер (cams.bin) из флеш-памяти W25Q128 -> PSRAM
  ------------------------------------------------------------------------------
  Флеш на отдельной шине SPI2 (пины HUD_FLASH_* в user_config.h), без файловой системы:
  cams.bin лежит «сырым» с адреса 0 (так же его пишет любой программатор).
  Загрузка идёт в фоновой задаче при старте: читает заголовок "CAM1", выделяет PSRAM,
  читает базу целиком и передаёт её в cam_alert. Нет флеши, базы или PSRAM —
  камеры просто не работают.

  Обновление базы с телефона (BLE, hud_ble_cams.cpp) использует тот же драйвер флеши:
  стирание секторов 4 КБ, запись страниц по 256 Б и hud_cams_reload() — перечитывание
  базы из флеши в PSRAM без перезагрузки HUD.
*/
#ifndef HUD_CAMS_H
#define HUD_CAMS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void hud_cams_start(void);     /* фоновая загрузка базы */
bool hud_cams_ready(void);     /* база загружена и годна */

/* Низкоуровневый доступ (для BLE-загрузки). Все вызовы потокобезопасны. */
uint32_t hud_cams_flash_capacity(void);   /* ёмкость флеши, байт; 0 — флеши нет */
uint32_t hud_cams_max_db_bytes(void);     /* наибольший cams.bin, который поместится в PSRAM */
bool     hud_cams_flash_erase(uint32_t addr);                         /* сектор 4 КБ, addr кратен 4096 */
bool     hud_cams_flash_write(uint32_t addr, const uint8_t *d, size_t n); /* n <= 256, в пределах страницы */
/* Выгрузить текущую базу, прочитать новую из флеши (len байт с адреса 0), проверить SHA-256
   (expect_sha != NULL) и формат, включить. false — камеры останутся выключены до следующей загрузки. */
bool     hud_cams_reload(const uint8_t *expect_sha, size_t len);
#ifdef __cplusplus
}
#endif
#endif
