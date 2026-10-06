/*
  hud_config.h — настройки сборки HUD
*/
#ifndef HUD_CONFIG_H
#define HUD_CONFIG_H

/* ---- Источник данных ----
   HUD_SRC_BLE  — сырые CAN-кадры от сниффера / BLE-гейта по BLE (протокол ACL)
   HUD_SRC_TWAI — свой трансивер TJA1051T/3 прямо на шине I-CAN,
                  встроенный контроллер TWAI в режиме LISTEN_ONLY
   HUD_SRC_FAKE — встроенный генератор кадров, для проверки без машины
   HUD_SRC_AUTO — работают оба: если по своему трансиверу (TWAI) идут кадры —
                  используется он, BLE отключается; нет кадров по CAN дольше
                  HUD_AUTO_CAN_HOLD_MS — используется BLE от сниффера / гейта
   Декодер и экран одни и те же, меняется только откуда приходят кадры. */
#define HUD_SRC_BLE          0
#define HUD_SRC_TWAI         1
#define HUD_SRC_FAKE         2
#define HUD_SRC_AUTO         3
#define HUD_DATA_SOURCE      HUD_SRC_AUTO
#define HUD_AUTO_CAN_HOLD_MS 1000   /* AUTO: столько без кадров по CAN — переходим на BLE */

/* BLE-защита: код доступа задаётся в secrets.h (шаблон secrets.example.h).
   Столько неудач сопряжения подряд — на экране подсказка «сопряжение не принято» */
#define HUD_BLE_PAIR_MSG_AFTER 3

/* ---- TWAI (для HUD_SRC_TWAI и HUD_SRC_AUTO) ----
   Super Mini: RX GPIO8, TX GPIO9 (не подключён).
   TJA1051T/3: CRX/RXD -> GPIO8, VCC -> 5V платы,
   VIO и S (Silent) -> 3V3 платы, CTX/TXD через 10 кОм -> VIO.
   TWAI работает в LISTEN_ONLY. GPIO9 оставить свободным. */
#define HUD_CAN_RX_PIN       8
#define HUD_CAN_TX_PIN       9
#define HUD_CAN_BITRATE_K    500    /* I-CAN MLB-Evo — 500 кбит/с */

/* совместимость со старым переключателем */
#ifdef HUD_FAKE_FRAMES
#if HUD_FAKE_FRAMES
#undef  HUD_DATA_SOURCE
#define HUD_DATA_SOURCE      HUD_SRC_FAKE
#endif
#endif

/* 1 = мигать строго по биту *_Kombi_Takt (фаза как на приборке).
   0 = бит такта используется только как признак "поворотник включён",
       мигаем своим таймером 500/500 мс (на случай рваного такта по BLE). */
#define HUD_BLINK_FROM_TAKT  1

/* Имя сниффера в эфире */
#define HUD_SNIFFER_NAME     "S3-CAN-Sniffer"

/* Lane Assist: LDW_02 0x397 (~80 мс). В ACL — интервал, а не onchange:
   при onchange стабильное состояние не присылается, и значок гас бы по таймауту. */
#define LKA_MIN_INTERVAL_MS  500

/* ---- Яркость подсветки дисплея (0..255): освещённость x колёсико ----
   Два входа, оба от машины:
     освещённость — датчик RLS_01: LS_Helligkeit_FW (до 6126 лк), выше — по
       RLS_Vorfeldhelligkeit_Boost (до ~30000 лк на прямом солнце);
     колёсико подсветки — BCM1_04.BCM1_Stellgroesse_Kl_58s (1..100).
   Четыре угла — подсветка при минимальной / максимальной освещённости и колёсике
   на минимуме / максимуме; между углами — плавно (по освещённости — логарифмически,
   как видит глаз; по колёсику — линейно). Ночная яркость NV3007: 2..40. */
#define HUD_BR_DARK_WHEEL_MIN     2      /* темно,  колёсико на минимуме  */
#define HUD_BR_DARK_WHEEL_MAX     40     /* темно,  колёсико на максимуме */
#define HUD_BR_BRIGHT_WHEEL_MIN   200    /* солнце, колёсико на минимуме  */
#define HUD_BR_BRIGHT_WHEEL_MAX   255    /* солнце, колёсико на максимуме */
#define HUD_LIGHT_LUX_DARK        0      /* «минимальная освещённость», лк  */
#define HUD_LIGHT_LUX_BRIGHT      30000  /* «максимальная» — прямое солнце, лк */
#define HUD_WHEEL_DEFAULT         100    /* колёсико, пока его положение не пришло, % */
#define HUD_BRIGHT_NO_DATA    170    /* пока нет данных о яркости (и без связи)       */
#define HUD_BRIGHT_STEP       4      /* плавность: шаг изменения за 50 мс             */

/* ---- Язык и единицы ----
   Настройки задаются здесь и применяются при каждой загрузке. */
#define HUD_LANG_RU    0
#define HUD_LANG_EN    1
#define HUD_UNITS_KM   0     /* км/ч, км, м                               */
#define HUD_UNITS_MI   1     /* mph, mi, ft (скорость ACC/лимитера тоже)  */

#define HUD_LANG       HUD_LANG_RU
#define HUD_UNITS      HUD_UNITS_KM
#define HUD_VOLUME_GAL 0     /* «сколько заправить»: 0 — литры, 1 — галлоны США */
#define HUD_TANK_L     54    /* начальный объём бака, л; 1..200, затем настройка BLE/NVS              */

/* ---- Бар ускорения (по CAN) ----
   По нижнему краю экрана под стрелкой навигации квадраты расходятся из центра: зелёные — разгон,
   красные — торможение. Источник:
     1) ESP_02.ESP_Laengsbeschl (0x101) — продольное ускорение от ESP, если
        этот кадр есть на I-CAN;
     2) иначе — производная скорости Kombi_01.KBI_angez_Geschw (0x30B).
   Полная шкала разгона — 0..HUD_ACCEL_REF_KMH за HUD_ACCEL_REF_SEC с
   (100 км/ч за 9 с = 3.09 м/с²), торможения — HUD_BRAKE_FULL_MS2 (6 м/с² —
   резкое, но не экстренное торможение; экстренное 8-10 упрётся в край). */
#define HUD_ACCEL_BAR         0      /* начальное состояние бара; затем BLE/NVS */
#define HUD_ACCEL_REF_KMH     100
#define HUD_ACCEL_REF_SEC     9
#define HUD_BRAKE_FULL_MS2    6
#define HUD_ACCEL_ON          0.40f  /* м/с²: бар загорается выше этого (сглаженного) ускорения */
#define HUD_ACCEL_OFF         0.20f  /* м/с²: и гаснет ниже этого — гистерезис, без мигания  */
#define HUD_ACCEL_SMOOTH_MS   600    /* сглаживание, мс                                       */
#define HUD_ACCEL_GAIN        0.25f  /* чувствительность бара: 0.25 — в 4 раза спокойнее,
                                        т.е. полная шкала = 12.4 м/с² разгона / 24 м/с² торможения */

/* ---- Знаки ограничения и превышение ----
   Основной знак: VZE_01 -> PSD явный знак (прогноз маршрута MIB, 0x462-0x464)
   -> PSD «по правилам». PSD работает и тогда, когда приборка знаков не показывает. */
#define HUD_PSD_LIMITS            1      /* знаки PSD      */
#define HUD_VZE_SIGNS             1      /* знаки VZE_01   */
#define HUD_PSD_LEGAL_DELAY_MS    1000   /* задержка смены «явный знак» -> «по правилам»      */
#define HUD_SIGN_CYCLE_MS         1000   /* несколько знаков — по очереди, каждый столько мс  */
/* Допуск превышения, км/ч. Обводка цифр скорости начинает проявляться при
   превышении HUD_OVERSPEED_START % допуска и становится полностью красной
   на 100 % (по умолчанию: 15 км/ч сверх знака — появляется, 20 — красная). */
#define HUD_OVERSPEED_TOL_KMH     20
#define HUD_OVERSPEED_START       75     /* % допуска */

/* ---- Side Assist (SWA_01, 0x30F) ----
   Полоски полос на значке ассистентов — красные, даже если Lane Assist и ACC
   выключены: машина в мёртвой зоне — полоска горит, попытка перестроения
   (поворотник при машине сбоку) — мигает. */
#define HUD_SWA               1

/* ---- Журналы в Serial по типам: 1 — печатать, 0 — нет ---- */
#define HUD_LOG_BLE    1   /* [BLE]  подключение к снифферу, запись и проверка ACL, ошибки   */
#define HUD_LOG_TWAI   1   /* [TWAI] запуск контроллера, ошибки, восстановление шины        */
#define HUD_LOG_SRC    1   /* [src]  какой источник сейчас используется (AUTO)              */
#define HUD_LOG_STAT   0   /* [BLE]/[TWAI] раз в 10 с: кадров, потери, макс. пауза скорости   */
#define HUD_LOG_CAN    0   /* [CAN]  раз в 10 с: счётчики и последние данные по каждому ID    */
#define HUD_LOG_DIM    0   /* [dim]  раз в 10 с: данные Dimmung_01 и выставленная подсветка   */
#define HUD_LOG_BAP    0   /* [nav]  каждая смена манёвра навигации, неизвестные коды         */
#define HUD_LOG_HUD    1   /* [hud]  построение экрана, пропадание скорости по таймауту       */
#define HUD_LOG_LVGL   1   /* [LVGL] сообщения LVGL (нужен ещё LV_USE_LOG 1 в lv_conf.h)      */

#endif
