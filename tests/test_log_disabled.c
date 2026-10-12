/* Expressions must compile, but no writer/helper definitions are needed. */
#include "ui_log.h"
#define TAG "OLED_DISABLED_PAYLOAD_TAG"
/* Intentionally has no definition: TAG checking must not introduce a call. */
const char *tag_with_side_effect(int *effects);
const char *payload_with_side_effect(void);
int main(void)
{
    int effects = 0;
    UI_LOGE(TAG, "OLED_DISABLED_PAYLOAD_E %d", ++effects);
    UI_LOGW(TAG, "OLED_DISABLED_PAYLOAD_W %s", payload_with_side_effect());
    UI_LOGI("OLED_DISABLED_PAYLOAD_LITERAL", "OLED_DISABLED_PAYLOAD_I %d", ++effects);
    UI_LOGD(TAG, "OLED_DISABLED_PAYLOAD_D %d", ++effects);
    UI_LOGV(tag_with_side_effect(&effects), "OLED_DISABLED_PAYLOAD_V %d", ++effects);
    ui_log_write(++effects, TAG, "OLED_DISABLED_PAYLOAD_GENERIC %s", payload_with_side_effect());
    return effects;
}
