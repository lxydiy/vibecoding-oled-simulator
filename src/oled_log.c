#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "oled_log.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

/* The public function-like macro filters at the call site. This single
 * implementation is shared by all callers/translation units. */
#undef oled_log_write

static uint64_t timestamp_ms(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000) + (uint64_t)now.tv_nsec / UINT64_C(1000000);
#endif
}

#if defined(__GNUC__)
__attribute__((visibility("default")))
#endif
void oled_log_write(int level, const char *tag, const char *format, ...)
{
    char record[1024];
    static const char letters[] = "NEWIDV";
    size_t used, available;
    int count;
    va_list args;
    if (level < OLED_LOG_ERROR || level > OLED_LOG_VERBOSE) return;
    count = snprintf(record, sizeof record - 1, "%c (%llu) %.96s: ",
                     letters[level], (unsigned long long)timestamp_ms(), tag ? tag : "-");
    if (count < 0) return;
    used = (size_t)count;
    if (used >= sizeof record - 1) used = sizeof record - 2;
    available = sizeof record - 1 - used;
    va_start(args, format);
    count = vsnprintf(record + used, available, format ? format : "", args);
    va_end(args);
    if (count < 0) return;
    used += (size_t)count < available ? (size_t)count : available - 1;
    while (used && (record[used - 1] == '\n' || record[used - 1] == '\r')) --used;
    record[used++] = '\n';
    /* One stdio write per record prevents character-level interleaving.
     * Fixed stack capacity; oversized messages are truncated with a newline. */
    (void)fwrite(record, 1, used, stderr);
    (void)fflush(stderr);
}
