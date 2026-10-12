#include <stdio.h>
#include <stdlib.h>
#include "oled_log.h"

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)
void test_log_other(void);

int main(void)
{
    int effects = 0;
    OLED_LOGE("test", "value=%d", ++effects);
    OLED_LOGW("test", "value=%d", ++effects);
    OLED_LOGI("test", "value=%d", ++effects);
    OLED_LOGD("test", "value=%d", ++effects);
    OLED_LOGV("test", "value=%d", ++effects);
    int enabled = 0;
    for (int level = 1; level <= 5; ++level)
        if (level >= OLED_LOG_MIN_LEVEL && level <= OLED_LOG_LEVEL && level <= OLED_LOG_LOCAL_LEVEL) ++enabled;
    CHECK(effects == enabled);
    int level_evaluations = OLED_LOG_INFO;
    oled_log_write(level_evaluations++, "generic", "hello");
    CHECK(level_evaluations == OLED_LOG_INFO + 1);
    oled_log_write(99, "ignored", "%d", ++effects);
    oled_log_write(0, "ignored", "%d", ++effects);
    CHECK(effects == enabled);
    test_log_other();
    puts("PASS");
    return 0;
}
