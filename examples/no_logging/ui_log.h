#ifndef UI_LOG_H
#define UI_LOG_H

/* Standalone production replacement for include/ui_log.h.
 * Ordinary C calls inside sizeof retain compiler diagnostics without output
 * or argument evaluation. The writer needs no definition or linked library.
 * Place this directory first on the application's include search path, or
 * copy this file into the hardware project's include directory.
 * Existing application sources keep: #include "ui_log.h"
 */
#define UI_LOG_NONE    0
#define UI_LOG_ERROR   1
#define UI_LOG_WARN    2
#define UI_LOG_INFO    3
#define UI_LOG_DEBUG   4
#define UI_LOG_VERBOSE 5

#ifndef UI_LOG_LEVEL
#define UI_LOG_LEVEL UI_LOG_NONE
#endif
#ifndef UI_LOG_MIN_LEVEL
#define UI_LOG_MIN_LEVEL UI_LOG_ERROR
#endif
#ifndef UI_LOG_LOCAL_LEVEL
#define UI_LOG_LOCAL_LEVEL UI_LOG_LEVEL
#endif

void ui_log_write(int level, const char *tag, const char *format, ...);

#define ui_log_write(level, tag, ...) \
    ((void)sizeof(((ui_log_write)(level, tag, __VA_ARGS__), 0)))

#define UI_LOGE(tag, ...) ui_log_write(UI_LOG_ERROR, tag, __VA_ARGS__)
#define UI_LOGW(tag, ...) ui_log_write(UI_LOG_WARN, tag, __VA_ARGS__)
#define UI_LOGI(tag, ...) ui_log_write(UI_LOG_INFO, tag, __VA_ARGS__)
#define UI_LOGD(tag, ...) ui_log_write(UI_LOG_DEBUG, tag, __VA_ARGS__)
#define UI_LOGV(tag, ...) ui_log_write(UI_LOG_VERBOSE, tag, __VA_ARGS__)
#endif
