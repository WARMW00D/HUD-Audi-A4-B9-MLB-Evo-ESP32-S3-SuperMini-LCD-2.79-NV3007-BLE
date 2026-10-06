#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "hud_data.h"
static int64_t now_us = 100000;
int64_t esp_timer_get_time(void) { return now_us; }
int main(void) {
    uint8_t frame[8] = {0};
    frame[3] = 123;
    can_decode_frame(0x30b, false, frame, 8);
    HudData d;
    hud_data_snapshot(&d);
    assert((d.valid & V_SPEED) && d.speed_kmh == 123);
    uint32_t valid = d.valid;
    can_decode_frame(0x6b2, false, frame, 8);
    hud_data_snapshot(&d);
    assert(d.valid == valid);
    assert(!(d.valid & (1u << 21)));
    now_us += 2000000;
    hud_data_snapshot(&d);
    assert(!(d.valid & V_SPEED));
    puts("PASS: speed decoding, removed logger clock, preserved validity bits, speed timeout.");
}
