@echo off
rem Build hud_sim.exe (HUD simulator for Windows, SuperMini firmware).
rem Needs: git + a compiler in PATH: zig (ziglang.org, one zip) or MinGW-w64 gcc.
rem LVGL 8.4.0 is cloned into .\lvgl on the first run; objects go to .\build.
rem
rem Firmware folder (supermini_hud), first match wins:
rem   1) argument:  build_win.bat D:\path\to\supermini_hud   (remembered in fw_path.txt)
rem   2) fw_path.txt next to this script (one line with the path)
rem   3) ..\..\supermini_hud  (this folder sits in tools\ of the repository)
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "FW=..\..\supermini_hud"
if exist fw_path.txt set /p FW=<fw_path.txt
if not "%~1"=="" set "FW=%~1"
set "FW=%FW:"=%"
set "FW=%FW:/=\%"
if "%FW:~-1%"=="\" set "FW=%FW:~0,-1%"
if not exist "%FW%\hud_mockup.c" goto :nofw
if not "%~1"=="" >fw_path.txt echo %FW%
echo Firmware: %FW%
for /f "tokens=2,3" %%a in ('findstr /b /c:"#define HUD_FW_VERSION_" "%FW%\hud_config.h"') do echo   %%a = %%b

set "CC="
where zig >nul 2>nul
if not errorlevel 1 set "CC=zig cc -target x86_64-windows-gnu"
if not defined CC (
  where gcc >nul 2>nul
  if not errorlevel 1 set "CC=gcc"
)
if not defined CC (
  echo ERROR: neither zig nor gcc found in PATH
  exit /b 1
)
echo Compiler: %CC%

if not exist lvgl\lvgl.h (
  git clone --depth 1 -b v8.4.0 https://github.com/lvgl/lvgl.git lvgl || exit /b 1
)

set CF=-O2 -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -I. -Ishim -Ilvgl -I"%FW%"
if not exist build\lv mkdir build\lv

rem ---- LVGL: compiled once (delete .\build to rebuild) ----
if not exist build\lv.rsp (
  echo [1/3] LVGL, a few minutes...
  if exist build\lv.tmp del build\lv.tmp
  set N=0
  for /r lvgl\src %%f in (*.c) do (
    set /a N+=1
    %CC% %CF% -w -c "%%f" -o build\lv\!N!.o || exit /b 1
    echo build/lv/!N!.o>>build\lv.tmp
  )
  ren build\lv.tmp lv.rsp
)

rem ---- HUD firmware: every .c of the firmware folder, unchanged (hud_mockup.c = the screen);
rem      can_decode.c is built through sim_decode_*.c (both platforms). Old objects are removed first,
rem      so a file dropped from the firmware can never stay in the exe. ----
echo [2/3] HUD firmware sources...
del /q build\*.o 2>nul
set "OBJS="
for %%f in ("%FW%\*.c") do (
  if /i not "%%~nf"=="can_decode" (
    %CC% %CF% -w -c "%%f" -o build\fw_%%~nf.o || exit /b 1
    set "OBJS=!OBJS! build/fw_%%~nf.o"
  )
)

echo [3/3] simulator...
%CC% %CF% -w -c sim_decode_mlb.c -o build\sim_decode_mlb.o || exit /b 1
%CC% %CF% -w -c sim_decode_mqb.c -o build\sim_decode_mqb.o || exit /b 1
%CC% %CF% -Wall -c sim_core.c -o build\sim_core.o || exit /b 1
%CC% %CF% -Wall -c sim_win.c -o build\sim_win.o || exit /b 1
%CC% -o hud_sim.exe build/sim_win.o build/sim_core.o build/sim_decode_mlb.o build/sim_decode_mqb.o %OBJS% @build/lv.rsp -municode -mwindows -lgdi32 -luser32 -lcomdlg32 -lcomctl32 -lshell32 -lole32 -lwinmm -lgdiplus -s || exit /b 1
echo OK: hud_sim.exe
exit /b 0

:nofw
echo ERROR: hud_mockup.c not found in "%FW%"
echo Pass the firmware folder once:  build_win.bat D:\path\to\supermini_hud
exit /b 1
