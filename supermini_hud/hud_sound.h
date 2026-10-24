/*
  hud_sound.h — passive speaker / buzzer through an NPN transistor
  ------------------------------------------------------------------------------
  GPIO (HUD_SND_PIN) -> 1 kOhm -> base of BC547, emitter to GND, speaker between
  +5 V and the collector in series with ~100 Ohm, 1N4148 across the speaker
  (cathode to +5 V). The pin is held LOW when silent, so no DC flows through
  the speaker. Tones are played by a low-priority task; callers never block.
*/
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    HUD_SND_BEEP = 0,   /* one short beep                      */
    HUD_SND_DOUBLE,     /* two short beeps                     */
    HUD_SND_RISING,     /* three rising tones (startup / info) */
    HUD_SND_WARN,       /* three fast high beeps (warning)     */
    HUD_SND_COUNT
} hud_sound_t;

void hud_sound_begin(void);
void hud_sound_play(hud_sound_t s);     /* non-blocking; ignored while disabled or queue is full */
void hud_sound_set_enabled(bool on);
bool hud_sound_enabled(void);
void hud_sound_set_volume(uint8_t pct); /* 0..100 %, 0 = silent */
uint8_t hud_sound_volume(void);
#ifdef __cplusplus
}
#endif
