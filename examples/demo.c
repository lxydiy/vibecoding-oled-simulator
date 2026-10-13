#include "oled_fb.h"
#include "oled_switch.h"
#include "ui_log.h"
#include "demo.h"
#include "font6x8.h"
#include <string.h>

static int last_key_mask;

void demo_init(void)
{
    /* Simulated firmware initialization before while (1): reset application
     * state, announce configuration, and draw the initial screen once. */
    last_key_mask = -1;
    UI_LOGI("demo", "init: OLED %dx%d, switches 1-4", OLED_WIDTH, OLED_HEIGHT);
    UI_LOGD("demo", "drawing initial text, lines and rectangles");
    demo_update();
}

/* Application helpers adapted from the reference User/hal/oled.c.
 * They remain private to this example, not part of the simulated HAL. */
static const uint8_t fill_bitmap[9] = {
    0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF
};

static void text(int16_t x, int16_t y, const char *str)
{
    if (!str || x >= OLED_WIDTH || y >= OLED_HEIGHT || (int32_t)y + 8 <= 0) return;
    while (*str && x < OLED_WIDTH) {
        unsigned char ch = (unsigned char)*str++;
        if (ch >= 32 && ch <= 122)
            oled_fb_draw_image(x, y, font6x8[ch - 32], 6, 8, false);
        x = (int16_t)((int32_t)x + 6);
    }
}

static void vline(int16_t x, int16_t top, int16_t bottom)
{
    if ((uint16_t)x >= OLED_WIDTH) return;
    if (top > bottom) { int16_t tmp = top; top = bottom; bottom = tmp; }
    if (top >= OLED_HEIGHT || bottom < 0) return;
    if (top < 0) top = 0;
    if (bottom >= OLED_HEIGHT) bottom = OLED_HEIGHT - 1;
    int first_page = top / 8, last_page = bottom / 8;
    uint8_t first_mask = (uint8_t)~fill_bitmap[top & 7u];
    uint8_t last_mask = fill_bitmap[(bottom & 7u) + 1u];
    uint8_t *dst = oled_buffer + first_page * OLED_WIDTH + x;
    if (first_page == last_page) {
        *dst |= (uint8_t)(first_mask & last_mask);
        return;
    }
    *dst |= first_mask;
    for (int page = first_page + 1; page < last_page; ++page) {
        dst += OLED_WIDTH;
        *dst = 0xFF;
    }
    dst += OLED_WIDTH;
    *dst |= last_mask;
}

static void hline(int16_t left, int16_t right, int16_t y)
{
    if ((uint16_t)y >= OLED_HEIGHT) return;
    if (left > right) { int16_t tmp = left; left = right; right = tmp; }
    if (left >= OLED_WIDTH || right < 0) return;
    if (left < 0) left = 0;
    if (right >= OLED_WIDTH) right = OLED_WIDTH - 1;
    uint8_t mask = (uint8_t)(1u << (y & 7u));
    uint8_t *dst = oled_buffer + (y / 8) * OLED_WIDTH + left;
    uint8_t *end = dst + (right - left + 1);
    do { *dst++ |= mask; } while (dst != end);
}

static void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    if ((x0 < 0 && x1 < 0) || (x0 >= OLED_WIDTH && x1 >= OLED_WIDTH) ||
        (y0 < 0 && y1 < 0) || (y0 >= OLED_HEIGHT && y1 >= OLED_HEIGHT)) return;
    if (x0 == x1) { vline(x0, y0, y1); return; }
    if (y0 == y1) { hline(x0, x1, y0); return; }
    int32_t x = x0, y = y0, end_x = x1, end_y = y1;
    int32_t dx = end_x >= x ? end_x - x : x - end_x;
    int32_t sx = x < end_x ? 1 : -1;
    int32_t dy = -(end_y >= y ? end_y - y : y - end_y);
    int32_t sy = y < end_y ? 1 : -1;
    int32_t error = dx + dy;
    for (;;) {
        if (x >= 0 && x < OLED_WIDTH && y >= 0 && y < OLED_HEIGHT)
            oled_fb_draw_dot((int16_t)x, (int16_t)y);
        if (x == end_x && y == end_y) break;
        int32_t twice = 2 * error;
        if (twice >= dy) { error += dy; x += sx; }
        if (twice <= dx) { error += dx; y += sy; }
    }
}

static void fill_rect(int16_t left, int16_t top, int16_t right, int16_t bottom)
{
    if (left > right || top > bottom || right < 0 || bottom < 0 ||
        left >= OLED_WIDTH || top >= OLED_HEIGHT) return;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right >= OLED_WIDTH) right = OLED_WIDTH - 1;
    if (bottom >= OLED_HEIGHT) bottom = OLED_HEIGHT - 1;
    uint16_t width = (uint16_t)(right - left + 1);
    int first_page = top / 8, last_page = bottom / 8;
    uint8_t first_mask = (uint8_t)~fill_bitmap[top & 7u];
    uint8_t last_mask = fill_bitmap[(bottom & 7u) + 1u];
    for (int page = first_page; page <= last_page; ++page) {
        uint8_t mask = 0xFF;
        if (page == first_page) mask &= first_mask;
        if (page == last_page) mask &= last_mask;
        uint8_t *dst = oled_buffer + page * OLED_WIDTH + left;
        if (mask == 0xFF) {
            memset(dst, 0xFF, width);
        } else {
            uint8_t *end = dst + width;
            do { *dst++ |= mask; } while (dst != end);
        }
    }
}

static void rectangle(int16_t x, int16_t y, int16_t right, int16_t bottom, bool filled)
{
    if (filled) {
        fill_rect(x, y, right, bottom);
    } else {
        line(x, y, right, y);
        line(x, bottom, right, bottom);
        line(x, y, x, bottom);
        line(right, y, right, bottom);
    }
}

/* Called by the executable host, never exported by oled_sim. */
void demo_update(void)
{
    int mask = 0;
    for (uint8_t id = 1; id <= 4; ++id)
        if (oled_switch_is_pressed(id)) mask |= 1 << (id - 1);
    if (mask == last_key_mask) return;
    if (last_key_mask >= 0) {
        for (int i = 0; i < 4; ++i)
            if ((mask ^ last_key_mask) & (1 << i))
                UI_LOGI("keys", "SW%d %s", i + 1, mask & (1 << i) ? "DOWN" : "UP");
    }
    last_key_mask = mask;

    oled_fb_clear_buffer();
    text(7, 2, "TEXT / LINE / RECT");
    line(0, 12, 127, 12);
    line(4, 17, 39, 30);
    line(4, 30, 39, 17);
    rectangle(49, 17, 83, 30, false);
    rectangle(94, 17, 123, 30, true);
    line(0, 35, 127, 35);
    for (int i = 0; i < 4; ++i) {
        int16_t x = (int16_t)(i * 32);
        bool pressed = (mask & (1 << i)) != 0;
        char label[] = "SW1";
        label[2] = (char)('1' + i);
        rectangle(x, 39, (int16_t)(x + 30), 63, false);
        text((int16_t)(x + 6), 42, label);
        text((int16_t)(x + (pressed ? 3 : 9)), 53, pressed ? "DOWN" : "UP");
    }
    oled_fb_send_buffer();
}
