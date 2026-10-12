#include "oled_fb.h"
#include "oled_switch.h"
#include "oled_log.h"
#include "demo.h"
#include "font6x8.h"

static int last_mask;

void demo_init(void)
{
    /* Simulated firmware initialization before while (1): reset application
     * state, announce configuration, and draw the initial screen once. */
    last_mask = -1;
    OLED_LOGI("demo", "init: OLED %dx%d, switches 1-4", OLED_WIDTH, OLED_HEIGHT);
    OLED_LOGD("demo", "drawing initial text, lines and rectangles");
    demo_update();
}

/* Application helpers adapted from the reference User/hal/oled.c.
 * They remain private to this example, not part of the simulated HAL. */
static void text(uint8_t x, uint8_t y, const char *str)
{
    unsigned column = x;
    while (*str && column < OLED_WIDTH) {
        unsigned char ch = (unsigned char)*str++;
        if (ch >= 32 && ch <= 122)
            oled_fb_draw_image((uint8_t)column, y, font6x8[ch - 32], 6, 8, false);
        column += 6;
    }
}

static void line(int x, int y, int end_x, int end_y)
{
    int dx = end_x >= x ? end_x - x : x - end_x;
    int sx = x < end_x ? 1 : -1;
    int dy = -(end_y >= y ? end_y - y : y - end_y);
    int sy = y < end_y ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        if (x >= 0 && x < OLED_WIDTH && y >= 0 && y < OLED_HEIGHT)
            oled_fb_draw_dot((uint8_t)x, (uint8_t)y);
        if (x == end_x && y == end_y) break;
        int twice = 2 * error;
        if (twice >= dy) { error += dy; x += sx; }
        if (twice <= dx) { error += dx; y += sy; }
    }
}

static void rectangle(int x, int y, int right, int bottom, bool filled)
{
    if (filled) {
        for (int row = y; row <= bottom; ++row) line(x, row, right, row);
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
    if (mask == last_mask) return;
    if (last_mask >= 0) {
        for (int i = 0; i < 4; ++i)
            if ((mask ^ last_mask) & (1 << i))
                OLED_LOGI("keys", "SW%d %s", i + 1, mask & (1 << i) ? "DOWN" : "UP");
    }
    last_mask = mask;

    oled_fb_clear_buffer();
    text(7, 2, "TEXT / LINE / RECT");
    line(0, 12, 127, 12);
    line(4, 17, 39, 30);
    line(4, 30, 39, 17);
    rectangle(49, 17, 83, 30, false);
    rectangle(94, 17, 123, 30, true);
    line(0, 35, 127, 35);
    for (int i = 0; i < 4; ++i) {
        int x = i * 32;
        bool pressed = (mask & (1 << i)) != 0;
        char label[] = "SW1";
        label[2] = (char)('1' + i);
        rectangle(x, 39, x + 30, 63, false);
        text((uint8_t)(x + 6), 42, label);
        text((uint8_t)(x + (pressed ? 3 : 9)), 53, pressed ? "DOWN" : "UP");
    }
    oled_fb_send_buffer();
}
