#include <assert.h>
#include <stdio.h>
#include "../hud_overspeed.h"
int main(void) {
    assert(hud_overspeed_opacity(-1, 20, 75) == 0);
    assert(hud_overspeed_opacity(0, 20, 75) == 0);
    assert(hud_overspeed_opacity(15, 20, 75) == 0);
    assert(hud_overspeed_opacity(17.5f, 20, 75) == 127);
    assert(hud_overspeed_opacity(20, 20, 75) == 255);
    assert(hud_overspeed_opacity(60, 20, 75) == 255);
    assert(hud_overspeed_opacity(0, 0, 75) == 0);
    assert(hud_overspeed_opacity(0.1f, 0, 75) == 255);
    assert(hud_overspeed_opacity(75, 100, 75) == 0);
    assert(hud_overspeed_opacity(100, 100, 75) == 255);
    assert(hud_overspeed_opacity(19, 20, 100) == 0);
    assert(hud_overspeed_opacity(20, 20, 100) == 255);
    puts("PASS: overspeed fade boundaries, configurable tolerance, zero-tolerance and hard-threshold cases.");
}
