#ifndef UI_LOG_H
#define UI_LOG_H

/* Configure macros BEFORE including this file. Formatting/output is compiled
 * once in src/ui_log.c, linked through oled::sim.
 * Format: I (monotonic_ms) tag: message. Default output: stderr. */
#define UI_LOG_NONE    0
#define UI_LOG_ERROR   1
#define UI_LOG_WARN    2
#define UI_LOG_INFO    3
#define UI_LOG_DEBUG   4
#define UI_LOG_VERBOSE 5

#ifndef UI_LOG_LEVEL
#define UI_LOG_LEVEL UI_LOG_INFO
#endif
#ifndef UI_LOG_MIN_LEVEL
#define UI_LOG_MIN_LEVEL UI_LOG_ERROR
#endif
#ifndef UI_LOG_LOCAL_LEVEL
#define UI_LOG_LOCAL_LEVEL UI_LOG_LEVEL
#endif

#if UI_LOG_LEVEL < 0 || UI_LOG_LEVEL > 5 || UI_LOG_LOCAL_LEVEL < 0 || UI_LOG_LOCAL_LEVEL > 5
#error "UI_LOG_LEVEL and UI_LOG_LOCAL_LEVEL must be in 0..5"
#endif
#if UI_LOG_MIN_LEVEL < 1 || UI_LOG_MIN_LEVEL > 5
#error "UI_LOG_MIN_LEVEL must be in 1..5"
#endif

void ui_log_write(int level, const char *tag, const char *format, ...);

/* sizeof preserves ordinary C call checking without evaluating the call.
 * The comma expression has type int (sizeof(void) is not standard C). */
#define UI_LOG_DISCARD_(level, tag, ...) \
    ((void)sizeof(((ui_log_write)(level, tag, __VA_ARGS__), 0)))

#if UI_LOG_LEVEL >= UI_LOG_MIN_LEVEL && UI_LOG_LOCAL_LEVEL >= UI_LOG_MIN_LEVEL
/* Filter before evaluating tag/format arguments. Evaluate level only once.
 * Parentheses around the function name avoid macro recursion. */
#define ui_log_write(level, tag, ...) do { \
    const int ui_log_requested_level_ = (level); \
    if (ui_log_requested_level_ >= UI_LOG_MIN_LEVEL && \
        ui_log_requested_level_ <= UI_LOG_LEVEL && \
        ui_log_requested_level_ <= UI_LOG_LOCAL_LEVEL) \
        (ui_log_write)(ui_log_requested_level_, tag, __VA_ARGS__); \
} while (0)
#else
#define ui_log_write(level, tag, ...) UI_LOG_DISCARD_(level, tag, __VA_ARGS__)
#endif

/* __VA_ARGS__ must contain at least the format required by ui_log_write. */
#if UI_LOG_MIN_LEVEL <= 1 && UI_LOG_LEVEL >= 1 && UI_LOG_LOCAL_LEVEL >= 1
#define UI_LOGE(tag, ...) (ui_log_write)(UI_LOG_ERROR, tag, __VA_ARGS__)
#else
#define UI_LOGE(tag, ...) UI_LOG_DISCARD_(UI_LOG_ERROR, tag, __VA_ARGS__)
#endif
#if UI_LOG_MIN_LEVEL <= 2 && UI_LOG_LEVEL >= 2 && UI_LOG_LOCAL_LEVEL >= 2
#define UI_LOGW(tag, ...) (ui_log_write)(UI_LOG_WARN, tag, __VA_ARGS__)
#else
#define UI_LOGW(tag, ...) UI_LOG_DISCARD_(UI_LOG_WARN, tag, __VA_ARGS__)
#endif
#if UI_LOG_MIN_LEVEL <= 3 && UI_LOG_LEVEL >= 3 && UI_LOG_LOCAL_LEVEL >= 3
#define UI_LOGI(tag, ...) (ui_log_write)(UI_LOG_INFO, tag, __VA_ARGS__)
#else
#define UI_LOGI(tag, ...) UI_LOG_DISCARD_(UI_LOG_INFO, tag, __VA_ARGS__)
#endif
#if UI_LOG_MIN_LEVEL <= 4 && UI_LOG_LEVEL >= 4 && UI_LOG_LOCAL_LEVEL >= 4
#define UI_LOGD(tag, ...) (ui_log_write)(UI_LOG_DEBUG, tag, __VA_ARGS__)
#else
#define UI_LOGD(tag, ...) UI_LOG_DISCARD_(UI_LOG_DEBUG, tag, __VA_ARGS__)
#endif
#if UI_LOG_MIN_LEVEL <= 5 && UI_LOG_LEVEL >= 5 && UI_LOG_LOCAL_LEVEL >= 5
#define UI_LOGV(tag, ...) (ui_log_write)(UI_LOG_VERBOSE, tag, __VA_ARGS__)
#else
#define UI_LOGV(tag, ...) UI_LOG_DISCARD_(UI_LOG_VERBOSE, tag, __VA_ARGS__)
#endif
#endif
