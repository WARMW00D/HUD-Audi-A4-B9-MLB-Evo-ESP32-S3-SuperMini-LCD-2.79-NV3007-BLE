#!/bin/sh
# Сборка hud_shot — ядро симулятора без окна (Linux/macOS/WSL): кадр HUD из лога в BMP.
#   ./hud_shot can_log_0004.txt 215 out.bmp [cams.bin [arrows.bin]]
# Настройки HUD — переменными окружения: SIM_LANG=1 SIM_UNITS=1 SIM_PSD=0 SIM_VZE=1 SIM_GAL=1 SIM_TANK=60 SIM_ACCEL=1 SIM_TOL=10 SIM_CAMS=1 SIM_BEEP=1
# Папка прошивки: FW=/путь/к/supermini_hud ./build_headless.sh (по умолчанию ../../supermini_hud)
set -e
cd "$(dirname "$0")"
FW=${FW:-../../supermini_hud}
[ -f lvgl/lvgl.h ] || git clone --depth 1 -b v8.4.0 https://github.com/lvgl/lvgl.git lvgl
CF="-O2 -w -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -I. -Ishim -Ilvgl -I$FW"
# все .c прошивки; can_decode.c идёт через sim_decode_*.c (две платформы), остальное в прошивке — C++ / железо
SRC=$(ls "$FW"/*.c | grep -v -e '/can_decode\.c$')
${CC:-cc} $CF sim_headless.c sim_core.c sim_decode_mlb.c sim_decode_mqb.c \
  $SRC $(find lvgl/src -name '*.c') -lm -o hud_shot
echo OK: hud_shot
