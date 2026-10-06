# CAN and gateway reference

Adapted from the Waveshare project README. Verification labels below are the original project’s observations, not new SuperMini hardware tests. Decoder source in `../supermini_hud/` is authoritative.

## Data sources (CAN)

All signals are Intel (little-endian). Extraction:

```c
static inline uint32_t sig(const uint8_t *d, int start, int len) {
    uint64_t raw = 0; for (int i = 7; i >= 0; i--) raw = (raw << 8) | d[i];
    return (raw >> start) & ((1ULL << len) - 1);
}
```

### Regular frames (11-bit)

| What | ID | Message | Signal | start\|len | Scale / values | Verified |
|---|---|---|---|---|---|---|
| Speed | 0x30B | Kombi_01 | KBI_V_Digital | 24\|9 | ×1 km/h | ✅ |
| ACC set speed | 0x2A6 | ACC_12 | ACC_Wunschgeschw_02 | 12\|10 | ×0.32 km/h, 1023 = none | ✅ |
| Limit from ACC | 0x2A6 | ACC_12 | ACC_Tempolimit | 0\|5 | table in `vze_table.h` | ⚠️ |
| Lead target | 0x2A6 | ACC_12 | ACC_Relevantes_Objekt_02 | 45\|2 | 0 none, 1 vehicle, 2 warning, 3 passive | ⚠️ |
| ACC distance | 0x2A6 | ACC_12 | ACC_Gesetzte_Zeitluecke | 37\|3 | 1…5 (decoded, not drawn) | ⚠️ |
| Lane centering | 0x2A6 | ACC_12 | ACA_Querfuehrung | 7\|2 | 2 = active | ⚠️ |
| Traffic jam assist | 0x2A6 | ACC_12 | STA_Primaeranz | 62\|2 | 1 ready, 2 active, 3 warning | ⚠️ |
| ACC status | 0x2A8 | ACC_14 | ACC_Status_Anzeige | 16\|3 | 2 standby, 3 active, 4 driver override | ✅ |
| Speed limiter | 0x31E | — (not in K-matrix) | set speed | 12\|10 | ×0.32 km/h, **1022 off**, 1023 on without speed | ✅ parked |
| Gear mode | 0x394 | WBA_03 | WBA_Fahrstufe_02 | 12\|4 | 1 P, 2 R, 3 N, 4 D, 5 S, 6 M, 8 E, 12 Offroad | ✅ |
| Gear number | 0x394 | WBA_03 | WBA_eing_Gang_02 | 24\|4 | 1…9 | ✅ D1…, ⚠️ 8th |
| Turn signals | 0x366 | Blinkmodi_02 | left / right phase, hazard | 27, 28, 20 | bit | ✅ |
| Sign | 0x181 | VZE_01 | VZE_Verkehrszeichen_1 | 11\|8 | code; limit = 5 × code (8 → 40, 12 → 60, 16 → 80) | ✅ three points |
| Sign: suppress / overspeed | 0x181 | VZE_01 | Anzeigeunterdrueck_1 / Warnung_1 | 50 / 35 | bit | ⚠️ |
| Lane Assist | 0x397 | LDW_02 | green / yellow LED | 62 / 61 | bit | ⚠️ |
| LKA lines | 0x397 | LDW_02 | Lernmodus_links / _rechts | 38\|2 / 36\|2 | 0 off, 1 not seen, 2 seen, 3 departure | ✅ 0→1 when switched on |
| LKA warning | 0x397 | LDW_02 | Warnung_links / _rechts | 56 / 57 | bit | ⚠️ |
| Doors, boot | 0x583 | ZV_02 | ZV_FT/BT/HFS/HBFS/HD_offen | 24…28 | bit | ✅ |
| Bonnet | 0x65A | BCM_01 | BCM1_MH_Schalter | 31 | bit | ✅ |
| Acceleration | 0x101 | ESP_02 | ESP_Laengsbeschl | 24\|10 | ×0.03125 − 16 m/s² | ⚠️ opendbc MLB, check presence on I-CAN |
| Acceleration (fallback) | 0x30B | Kombi_01 | KBI_angez_Geschw (derivative) | 48\|10 | ×0.32 km/h | ✅ layout |
| Brakes | 0x106 | ESP_05 | ESP_Bremsdruck / ESP_Fahrer_bremst | 16\|10 / 26 | ×0.3 − 30 bar / bit | ⚠️ opendbc MLB |
| PSD: segments | 0x462 | PSD_04 | Segment_ID / Vorgaenger / Segmentlaenge / Strassenkategorie / Bebauung | 0\|6 / 6\|6 / 12\|7 / 19\|3 / 43 | length ×2 m | ✅ drives 29.09 |
| PSD: position | 0x463 | PSD_05 | Pos_Segment_ID / Pos_Segmentlaenge (remaining) | 0\|6 / 6\|7 | ×2 m | ✅ |
| PSD: limits | 0x464 | PSD_06 mux 2 | Ges_Segment_ID / Offset / Geschwindigkeit / Typ / Ueberholverbot / Gesetzlich_Kategorie | 3\|6 / 9\|7 / 16\|5 / 21\|2 / 49\|2 / 56\|3 | code → km/h (lower bound), 23 = end | ✅ |
| Trip computer | 0x17330F10 (29-bit) | BAP_BC, LSG 0x0F, from the cluster | fct 0x18 "since start" / 0x19 long-term: [0–1] consumption ×0.1 L/100 (0xFFFF none), [6–7] distance ×0.1 km, [11] time, min, [15–16] avg speed ×0.1; 0x16 range, km; 0x17 odometer ×0.1 km; 0x1C fuel, % | LE | ✅ drives 0006/0010 |
| Side Assist | 0x30F | SWA_01 | SWA_Infostufe_SWA_li / _re, SWA_Warnung_SWA_li / _re | 26 / 42, 27 / 43 | bit: car in blind spot / warning | ⚠️ opendbc MLB, check on I-CAN |
| Dimmer wheel | 0x64F | BCM1_04 | BCM1_Stellgroesse_Kl_58s | 25\|7 | 1–100 % | ✅ log 0003 |
| Light sensor | 0x5A0 | RLS_01 | LS_Helligkeit_FW / LS_Helligkeit_IR / RLS_Vorfeldhelligkeit_Boost / RS_Regenmenge | 8\|10 / 0\|8 / 35\|4 / 24\|4 | ×6 lx (≤1021) / ×400 lx / 0–15 / ×10 % | ✅ drives 0006/0010 |
| Date and time | 0x6B2 | Diagnose_01 | UH_Jahr / Monat / Tag / Stunde / Minute / Sekunde | 28\|7 / 35\|4 / 39\|5 / 44\|5 / 49\|6 / 55\|6 | year +2000 | ⚠️ opendbc; frame present on I-CAN |
| Fuel | 0x107 | Motor_04 | MO_KVS (counter, µL) | 48\|15 | wraps at 32768 | ⚠️ opendbc MLB |
| Pre sense | 0x2A9 | ACC_15 | AWV_Warnung | 16\|3 | 0 none, 1 latent, 2 warning, 3 acute, 4 braking, 5 take over, 6 turning | ⚠️ from opendbc MQB |
| Brightness | 0x5F0 | Dimmung_01 | DI_KL_58xd / DI_Display_Nachtdesign | 0\|8 / 15 | final display brightness 10–100 % (254 Init, 255 error) / night design | ✅ log 0003, dimmer wheel |

`LDW_02`: **do not use** bits 12–15 (`LDW_Gong`, `LDW_SW_Warnung_*`) — on this car byte 1
is always `0x40`. When LKA is switched on while parked, LEDs 61/62 do not change but the
lines go to 1 — so "LKA on" = any line non-zero.

### Navigation: BAP Navigation_SD (29-bit)

IDs `0x17333210` and `0x17333211` (MIB → displays). BAP framing:

- `b0.bit7 = 0` — single frame: header `b0<<8 | b1`, data from byte 2;
- `b0 = 10cc LLLL` — start of a multi-frame message: length `(b0&0xF)<<8 | b1`, header in b2..b3;
- `b0 = 11cc SSSS` — continuation, channel `cc`, sequence `SSSS`.

Header: `opcode = hdr>>12 & 7` (3 = HeartbeatStatus, 4 = Status), `lsg = hdr>>6 & 0x3F`
(must be `0x32`), `fct = hdr & 0x3F`.

| fct | Function | Used for |
|---|---|---|
| 0x11 | RG_Status | 1 = route guidance active (main switch for showing navigation) |
| 0x12 | DistanceToNextManeuver | distance ×10 (LE), unit, bar graph % |
| 0x15 | DistanceToDestination | distance to destination |
| 0x16 | TimeToDestination | type (travel time / arrival), hours, minutes |
| 0x17 | ManeuverDescriptor | up to 3 maneuvers in a row: MainElement, Direction, Z-level, side-street count + their directions. The first (main arrow) and second (next) are used |

`Direction`: 0x00 straight, 0x40 left, 0x80 back, 0xC0 right (360/256 deg, counter-clockwise).

### Freshness timeouts

| Field | Timeout | Why |
|---|---|---|
| Speed, ACC, gear mode, gear number, limiter, jam assist | 1 s | frames arrive 5–10 times/s after the ACL |
| Sign, Lane Assist, doors | 1.5 s | intervals of 500–1000 ms |
| Turn signals | 2.5 s | `Blinkmodi_02` is sent once per second when idle |
| Bonnet | 3 s | `BCM_01` is sent once per second |
| Navigation | 60 s | BAP sends Status only on change, heartbeat ~25 s |

---

## Sniffer → HUD BLE protocol

The full protocol description on the sniffer/gateway side is in [esp32-CAN-sniffer-logger-screener](https://github.com/WARMW00D/esp32-CAN-sniffer-logger-screener). Below is what the HUD needs.

Service `A1B2C3D4-0001-41A2-9E3B-000000000001`, device `S3-CAN-Sniffer`.

| UUID | Properties | Purpose |
|---|---|---|
| `…0007` | WRITE, READ | ACL rule list |
| `…0008` | NOTIFY | stream of frames that passed the ACL |

Sequence: connect with MTU 247 → subscribe to `…0008` → write the ACL to `…0007` → the HUD
reads it back and compares (Serial prints "ACL прочитан обратно — совпадает").

**ACL format:** `[0x01][12-byte rule] × N`. Rule: `flags` (bit0 PERMIT, bit1 EXT,
bit2 ONCHANGE, bit3 ANYFMT), `reserved`, `minIntervalMs` (2 bytes), `id` (4), `mask` (4).
The first matching rule wins, with an implicit `deny any` at the end.

**Stream format:** `[count][flags: bit0 loss][frame]×count`, frame =
`ts(2) idf(4: bit31 EXT, bit30 RTR) dlc(1) data(dlc)`.

### HUD rule list (20 rules, 241 bytes)

| ID | Interval | Purpose |
|---|---|---|
| 0x30B | 100 ms | speed |
| 0x2A6, 0x2A8 | 200 ms | ACC, traffic jam assist |
| 0x31E | 200 ms | limiter (has CRC and counter) |
| 0x2A9 | 100 ms | pre sense |
| 0x5F0 | 100 ms | brightness (the wheel sends frames every ~60 ms) |
| 0x5A0 | 200 ms | light sensor |
| 0x64F | 100 ms | dimmer wheel |
| 0x30F | 100 ms | Side Assist (CRC — no ONCHANGE) |
| 0x462/0x463 (mask `0x7FE`), 0x464 | — | PSD: a stream of multiplexes, rate filters would lose records |
| 0x394 | 200 ms | gear mode and gear number |
| 0x366 | — | turn signals: every phase change is needed |
| 0x181 | 500 ms | signs |
| 0x397 | 500 ms | Lane Assist |
| 0x583 | 1000 ms | doors, boot |
| 0x65A | — | bonnet (already once per second) |
| 0x17330F10 (EXT) | — | BAP_BC: cluster trip computer, multi-frame |
| 0x17333210/11 (EXT, mask `0x1FFFFFFE`) | — | BAP navigation: multi-frame, rate limiting would break it |

**Why intervals instead of `ONCHANGE`.** With `ONCHANGE` an unchanged value is never sent
again: if the speed or an open door stays the same longer than the timeout, the field
disappears, and after a reconnect the HUD does not know the current state. Also, frames
with a CRC and counter (0x31E, 0x394) change every time, so `ONCHANGE` filters nothing.

---

