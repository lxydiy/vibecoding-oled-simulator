/* Must compile without any logging-only declarations or platform headers. */
#include "oled_log.h"
int main(void)
{
    int effects = 0;
    OLED_LOGE(undefined_tag, "OLED_DISABLED_PAYLOAD_E %d", ++effects);
    OLED_LOGW(undefined_tag, "OLED_DISABLED_PAYLOAD_W %s", undefined_helper());
    OLED_LOGI(undefined_tag, "OLED_DISABLED_PAYLOAD_I %d", ++effects);
    OLED_LOGD(undefined_tag, "OLED_DISABLED_PAYLOAD_D %d", ++effects);
    OLED_LOGV(undefined_tag, "OLED_DISABLED_PAYLOAD_V %d", ++effects);
    oled_log_write(undefined_level, undefined_tag, "OLED_DISABLED_PAYLOAD_GENERIC", undefined_helper());
    return effects;
}
