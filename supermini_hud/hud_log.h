#ifndef HUD_LOG_H
#define HUD_LOG_H
#ifdef __cplusplus
extern "C" {
#endif
/* Serial diagnostics only. No filesystem, storage task, or SD dependency. */
void hud_log_write(const char *s);
void arduino_printf(const char *format, ...);
#ifdef __cplusplus
}
#endif
#endif
