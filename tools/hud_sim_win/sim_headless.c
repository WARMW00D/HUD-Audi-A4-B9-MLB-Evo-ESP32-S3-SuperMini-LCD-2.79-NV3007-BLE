/*
  sim_headless.c — проверка ядра без окна (любая ОС): проиграть лог до момента T и сохранить кадр HUD в BMP.
  Сборка (Linux): см. build_headless.sh.   Запуск: hud_shot log.txt сек out.bmp [cams.bin [arrows.bin]]
*/
#include "sim_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void sim_plat_log(const char *s) { fputs(s, stdout); }
void sim_plat_play(const int16_t *pcm, uint32_t n, uint32_t rate, uint8_t vol) { (void)pcm; printf("[sim] звук: %u сэмплов, %u Гц, громкость %u\n", n, rate, vol); }

static int env_int(const char *k, int def) { const char *e = getenv(k); return e ? atoi(e) : def; }

static void *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = (char *)malloc((size_t)n + 1);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    fclose(f); b[n] = 0; *len = (size_t)n; return b;
}

static void save_bmp(const char *path)
{
    FILE *f = fopen(path, "wb"); if (!f) return;
    uint32_t sz = 54 + SIM_W * SIM_H * 4;
    uint8_t h[54] = { 'B', 'M' };
    int32_t w = SIM_W, hh = -SIM_H;
    memcpy(h + 2, &sz, 4); h[10] = 54; h[14] = 40; memcpy(h + 18, &w, 4); memcpy(h + 22, &hh, 4);
    h[26] = 1; h[28] = 32;
    fwrite(h, 1, 54, f); fwrite(sim_fb, 4, SIM_W * SIM_H, f); fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "hud_shot log.txt sec out.bmp [cams.bin [arrows.bin]]\n"); return 1; }
    size_t n; char *t;
    sim_init();
    {   /* настройки HUD из переменных окружения (по умолчанию — из hud_config.h): SIM_LANG=1 SIM_UNITS=1 SIM_PSD SIM_VZE SIM_GAL SIM_TANK SIM_ACCEL SIM_TOL SIM_CAMS SIM_BEEP */
        sim_settings_t c; sim_settings_defaults(&c);
        c.lang = (uint8_t)env_int("SIM_LANG", c.lang);   c.units = (uint8_t)env_int("SIM_UNITS", c.units);
        c.psd = (uint8_t)env_int("SIM_PSD", c.psd);      c.vze = (uint8_t)env_int("SIM_VZE", c.vze);
        c.gallons = (uint8_t)env_int("SIM_GAL", c.gallons); c.tank_l = (uint8_t)env_int("SIM_TANK", c.tank_l);
        c.accel_bar = (uint8_t)env_int("SIM_ACCEL", c.accel_bar); c.tol = (uint8_t)env_int("SIM_TOL", c.tol);
        c.cams = (uint8_t)env_int("SIM_CAMS", c.cams);   c.cam_beep = (uint8_t)env_int("SIM_BEEP", c.cam_beep);
        sim_settings_apply(&c);
    }
    if (getenv("SIM_PLATFORM") && !strcmp(getenv("SIM_PLATFORM"), "MQB")) sim_set_platform(SIM_PLATFORM_MQB);   /* по умолчанию MLB-Evo */
    if (argc > 4 && (t = (char *)slurp(argv[4], &n))) printf("[sim] cams.bin: %s\n", sim_set_cams(t, n) ? "ok" : "ошибка");
    if (argc > 5 && (t = (char *)slurp(argv[5], &n))) printf("[sim] arrows.bin: %s\n", sim_set_arrows(t, n) ? "ok" : "ошибка");
    if (!strcmp(argv[1], "-")) {                         /* без лога: «нет связи» — заставка; sec — сколько секунд прожить */
        for (int i = 0; i < (int)(atof(argv[2]) * 100); i++) sim_step(10);
        save_bmp(argv[3]);
        return 0;
    }
    t = (char *)slurp(argv[1], &n);
    if (!t || !sim_load_log(t, n, false)) { fprintf(stderr, "не удалось прочитать лог\n"); return 1; }
    free(t);
    uint32_t nm, ns; sim_maneuvers(&nm); sim_nav_spans(&ns);
    printf("[sim] кадров %u, длительность %.1f с, манёвров %u, участков навигации %u\n", sim_frames_total(), sim_dur_ms() / 1000.0, nm, ns);
    double sec = atof(argv[2]);
    if (getenv("SIM_PLAY_FROM")) {                       /* проиграть подряд от SIM_PLAY_FROM до sec */
        sim_seek_ms((uint32_t)(atof(getenv("SIM_PLAY_FROM")) * 1000));
        sim_play(true);
        for (int i = 0; i < 100; i++) sim_step(10);      /* 1 с на первую отрисовку — в замер не входит */
        uint32_t n0, p0, t0, n1, p1, t1;
        sim_refresh_stats(&n0, &p0, &t0);
        while (sim_pos_ms() < (uint32_t)(sec * 1000) && !sim_take_eof()) sim_step(10);
        sim_refresh_stats(&n1, &p1, &t1);
        if (t1 > t0) printf("[sim] перерисовок/с %.1f, в панель %.0f КБ/с (полный кадр = %.0f КБ)\n", (n1 - n0) * 1000.0 / (t1 - t0),
                            (p1 - p0) * 2.0 / 1024.0 * 1000.0 / (t1 - t0), SIM_W * SIM_H * 2 / 1024.0);
    } else {
        sim_seek_ms((uint32_t)(sec * 1000));
        sim_play(true);
        for (int i = 0; i < 150; i++) sim_step(10);      /* 1.5 с — экран успевает обновиться */
    }
    save_bmp(argv[3]);
    return 0;
}
