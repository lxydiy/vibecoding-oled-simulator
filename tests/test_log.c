#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "ui_log.h"

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)
void test_log_other(void);

static struct {
    int level;
    const char *tag;
    char message[64];
} records[8];
static unsigned record_count;

/* Use the writer's real signature without linking the output implementation. */
void (ui_log_write)(int level, const char *tag, const char *format, ...)
{
    CHECK(record_count < sizeof records / sizeof records[0]);
    records[record_count].level = level;
    records[record_count].tag = tag;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(records[record_count].message, sizeof records[record_count].message, format, args);
    va_end(args);
    CHECK(written >= 0 && (size_t)written < sizeof records[record_count].message);
    ++record_count;
}

int main(void)
{
    int effects = 0;
    UI_LOGE("test", "value=%d", ++effects);
    UI_LOGW("test", "value=%d", ++effects);
    UI_LOGI("test", "value=%d", ++effects);
    UI_LOGD("test", "value=%d", ++effects);
    UI_LOGV("test", "value=%d", ++effects);
    int enabled = 0;
    for (int level = 1; level <= 5; ++level) {
        if (level >= UI_LOG_MIN_LEVEL && level <= UI_LOG_LEVEL && level <= UI_LOG_LOCAL_LEVEL) {
            char expected[32];
            CHECK(records[enabled].level == level);
            CHECK(strcmp(records[enabled].tag, "test") == 0);
            snprintf(expected, sizeof expected, "value=%d", enabled + 1);
            CHECK(strcmp(records[enabled].message, expected) == 0);
            ++enabled;
        }
    }
    CHECK(record_count == (unsigned)enabled);
    CHECK(effects == enabled);
    int level_evaluations = UI_LOG_INFO;
    ui_log_write(level_evaluations++, "generic", "hello");
    CHECK(level_evaluations == UI_LOG_INFO + 1);
    CHECK(record_count == (unsigned)enabled + 1);
    CHECK(records[enabled].level == UI_LOG_INFO);
    CHECK(strcmp(records[enabled].tag, "generic") == 0);
    CHECK(strcmp(records[enabled].message, "hello") == 0);
    ui_log_write(99, "ignored", "%d", ++effects);
    ui_log_write(0, "ignored", "%d", ++effects);
    CHECK(effects == enabled);
    test_log_other();
    CHECK(record_count == (unsigned)enabled + 2);
    CHECK(records[enabled + 1].level == UI_LOG_INFO);
    CHECK(strcmp(records[enabled + 1].tag, "other") == 0);
    CHECK(strcmp(records[enabled + 1].message, "second translation unit") == 0);
    puts("PASS");
    return 0;
}
