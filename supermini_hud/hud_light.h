/*
  hud_light.h — optional photoresistor on an ADC1 GPIO
  ------------------------------------------------------------------------------
  Divider: photoresistor between 3V3 and HUD_LDR_PIN, 10 kOhm from the pin to
  GND, optional 100 nF from the pin to GND. The sensor is used only when the
  vehicle RLS_01 light value is unavailable.
*/
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
void hud_light_poll(void);
void hud_light_force(bool on);
void hud_light_sample_now(void);
int  hud_light_mv(void);
bool hud_light_enabled(void);
void hud_light_set_enabled(bool on);
void hud_light_get_cal(int *dark_mv, int *bright_mv);
void hud_light_set_cal(int dark_mv, int bright_mv);
bool hud_light_cal_valid(int dark_mv, int bright_mv);
int  hud_light_lux(void);
#ifdef __cplusplus
}
#endif
