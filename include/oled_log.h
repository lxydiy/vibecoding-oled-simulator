#ifndef OLED_LOG_H
#define OLED_LOG_H

/* Configure macros BEFORE including this file. Formatting/output is compiled
 * once in src/oled_log.c, linked through oled::sim.
 * Format: I (monotonic_ms) tag: message. Default output: stderr. */
#define OLED_LOG_NONE    0
#define OLED_LOG_ERROR   1
#define OLED_LOG_WARN    2
#define OLED_LOG_INFO    3
#define OLED_LOG_DEBUG   4
#define OLED_LOG_VERBOSE 5

#ifndef OLED_LOG_LEVEL
#define OLED_LOG_LEVEL OLED_LOG_INFO
#endif
#ifndef OLED_LOG_MIN_LEVEL
#define OLED_LOG_MIN_LEVEL OLED_LOG_ERROR
#endif
#ifndef OLED_LOG_LOCAL_LEVEL
#define OLED_LOG_LOCAL_LEVEL OLED_LOG_LEVEL
#endif

#if OLED_LOG_LEVEL < 0 || OLED_LOG_LEVEL > 5 || OLED_LOG_LOCAL_LEVEL < 0 || OLED_LOG_LOCAL_LEVEL > 5
#error "OLED_LOG_LEVEL and OLED_LOG_LOCAL_LEVEL must be in 0..5"
#endif
#if OLED_LOG_MIN_LEVEL < 1 || OLED_LOG_MIN_LEVEL > 5
#error "OLED_LOG_MIN_LEVEL must be in 1..5"
#endif

#if OLED_LOG_LEVEL >= OLED_LOG_MIN_LEVEL && OLED_LOG_LOCAL_LEVEL >= OLED_LOG_MIN_LEVEL
#if defined(__GNUC__)
__attribute__((format(printf, 3, 4)))
#endif
void oled_log_write(int level, const char *tag, const char *format, ...);

/* Filter before evaluating tag/format arguments. Evaluate level only once.
 * Parentheses around the function name avoid macro recursion. */
#define oled_log_write(level, ...) do { \
    const int oled_log_requested_level_ = (level); \
    if (oled_log_requested_level_ >= OLED_LOG_MIN_LEVEL && \
        oled_log_requested_level_ <= OLED_LOG_LEVEL && \
        oled_log_requested_level_ <= OLED_LOG_LOCAL_LEVEL) \
        (oled_log_write)(oled_log_requested_level_, __VA_ARGS__); \
} while (0)
#else
#define oled_log_write(...) ((void)0)
#endif

/* Preprocessor removal, including argument tokens, works even at -O0. */
#if OLED_LOG_MIN_LEVEL <= 1 && OLED_LOG_LEVEL >= 1 && OLED_LOG_LOCAL_LEVEL >= 1
#define OLED_LOGE(...) (oled_log_write)(OLED_LOG_ERROR, __VA_ARGS__)
#else
#define OLED_LOGE(...) ((void)0)
#endif
#if OLED_LOG_MIN_LEVEL <= 2 && OLED_LOG_LEVEL >= 2 && OLED_LOG_LOCAL_LEVEL >= 2
#define OLED_LOGW(...) (oled_log_write)(OLED_LOG_WARN, __VA_ARGS__)
#else
#define OLED_LOGW(...) ((void)0)
#endif
#if OLED_LOG_MIN_LEVEL <= 3 && OLED_LOG_LEVEL >= 3 && OLED_LOG_LOCAL_LEVEL >= 3
#define OLED_LOGI(...) (oled_log_write)(OLED_LOG_INFO, __VA_ARGS__)
#else
#define OLED_LOGI(...) ((void)0)
#endif
#if OLED_LOG_MIN_LEVEL <= 4 && OLED_LOG_LEVEL >= 4 && OLED_LOG_LOCAL_LEVEL >= 4
#define OLED_LOGD(...) (oled_log_write)(OLED_LOG_DEBUG, __VA_ARGS__)
#else
#define OLED_LOGD(...) ((void)0)
#endif
#if OLED_LOG_MIN_LEVEL <= 5 && OLED_LOG_LEVEL >= 5 && OLED_LOG_LOCAL_LEVEL >= 5
#define OLED_LOGV(...) (oled_log_write)(OLED_LOG_VERBOSE, __VA_ARGS__)
#else
#define OLED_LOGV(...) ((void)0)
#endif
#endif
