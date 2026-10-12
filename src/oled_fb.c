#include "oled_fb.h"
#include "sim_backend.h"
#include <string.h>

static uint8_t buffer[BUFFER_LENGTH];
/* RAM sent to the display is separate from the application's draw buffer. */
static uint8_t displayed[BUFFER_LENGTH];
static bool enabled;
static uint8_t contrast = 0xCF;

static void present(void) { sim_publish(displayed, enabled, contrast); }

OLED_EXPORT void oled_fb_send_buffer(void)
{
    memcpy(displayed, buffer, sizeof displayed);
    present();
}

OLED_EXPORT void oled_fb_fill_buffer(uint8_t item) { memset(buffer, item, sizeof buffer); }
OLED_EXPORT void oled_fb_clear_buffer(void) { oled_fb_fill_buffer(0); }
OLED_EXPORT void oled_fb_enable(bool en) { enabled = en; present(); }
OLED_EXPORT void oled_fb_before_fadein(void) { oled_fb_enable(false); }

OLED_EXPORT void oled_fb_fadeout(uint8_t step)
{
    if (!step) step = 1;
    for (int value = 0xF0; value > 0; value -= step) {
        contrast = (uint8_t)value;
        present();
        sim_sleep(4);
    }
    contrast = 0;
    oled_fb_enable(false);
}

OLED_EXPORT void oled_fb_fadein(uint8_t step)
{
    if (!step) step = 1;
    enabled = true;
    for (int value = 0; value < 0xF0; value += step) {
        contrast = (uint8_t)value;
        present();
        sim_sleep(4);
    }
    contrast = 0xCF;
    present();
}

OLED_EXPORT void oled_fb_draw_dot(uint8_t x, uint8_t y)
{
    if (x < OLED_WIDTH && y < OLED_HEIGHT)
        buffer[(y / 8u) * OLED_WIDTH + x] |= (uint8_t)(1u << (y % 8u));
}

OLED_EXPORT void oled_fb_draw_image(uint8_t x, uint8_t y, const uint8_t *bitmap,
                                   uint8_t w, uint8_t h, bool invert)
{
    if (!bitmap || x >= OLED_WIDTH || y >= OLED_HEIGHT) return;
    /* Page-major, vertical LSB-first bytes; only valid image bits are drawn.
     * Wide coordinates prevent uint8_t wraparound at the right/bottom edges. */
    for (unsigned row = 0; row < h && (unsigned)y + row < OLED_HEIGHT; ++row) {
        for (unsigned col = 0; col < w && (unsigned)x + col < OLED_WIDTH; ++col) {
            uint8_t bits = bitmap[(row / 8u) * w + col];
            if (invert) bits = (uint8_t)~bits;
            if (bits & (1u << (row % 8u)))
                oled_fb_draw_dot((uint8_t)(x + col), (uint8_t)(y + row));
        }
    }
}

OLED_EXPORT void oled_fb_init(void)
{
    sim_open();
    contrast = 0xCF;
    enabled = true;
    oled_fb_clear_buffer();
    oled_fb_send_buffer();
}
