# План переноса изменений в Waveshare HUD

Эта заметка фиксирует изменения, которые нужно перенести в архивную версию Waveshare при следующем обновлении проекта.

## Уже сделано в SuperMini-версии

1. Настройки через BLE/NVS:
   - объём бака (`HUD_TANK_L`, `hud_set_tank_l`, `hud_get_tank_l`);
   - включение/выключение бара ускорения;
   - допуск превышения скорости (`HUD_OVERSPEED_TOL_KMH`, `hud_set_overspeed_tol`, `hud_get_overspeed_tol`).
2. Цвет собственной машины:
   - зелёный только при `ACC_Status_Anzeige = 3`;
   - `ACC status = 4` — белый;
   - один Lane Assist не делает машину зелёной.
3. Side Assist (`SWA_01`, `0x30F`) имеет приоритет над цветом линий Lane Assist.
4. VZE:
   - `VZE_01`, `0x181`, знаки 1–3;
   - `VZE_02`, `0x29C`, знаки 4–5;
   - подавление знаков и replay-диагностика.
5. Навигация:
   - журнал исходного BAP-кадра `0x17`;
   - отдельный журнал для `MainElement 0x15/0x16` (кольцо), включая `Direction`, сектор и `side_n`.
6. Для replay временно включён `HUD_LOG_BAP = 1`.

## Файлы для переноса

- `supermini_hud/hud_config.h`
- `supermini_hud/hud_data.h`
- `supermini_hud/can_decode.c`
- `supermini_hud/hud_mockup.c`
- `supermini_hud/hud_overspeed.h`
- BLE/NVS-файлы настроек, если в Waveshare-архиве их структура отличается.

Источник CAN-описаний: `supermini_hud/CAN_INFOTAINMENT.md` и приложенная K-Matrix MLB-Evo V8.21.

Replay показал, что одинаковые `MainElement 0x15` и `Direction 0x40` могут соответствовать разным съездам. Номер съезда отдельным полем в принятых кадрах не передаётся, поэтому навигация использует исходный `Direction` без пересчёта по курсу автомобиля.
