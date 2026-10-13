#include "oled_fb.h"
#include "sim_backend.h"
#include <string.h>

OLED_EXPORT uint8_t oled_buffer[BUFFER_LENGTH];
/* RAM sent to the display is separate from the application's draw buffer. */
static uint8_t displayed[BUFFER_LENGTH];
static bool enabled;
static uint8_t contrast = 0xCF;

static void present(void) { sim_publish(displayed, enabled, contrast); }

OLED_EXPORT void oled_fb_send_buffer(void)
{
    memcpy(displayed, oled_buffer, sizeof displayed);
    present();
}

OLED_EXPORT void oled_fb_fill_buffer(uint8_t item) { memset(oled_buffer, item, sizeof oled_buffer); }
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

OLED_EXPORT void oled_fb_draw_dot(int16_t x, int16_t y)
{
    if ((uint16_t)x >= OLED_WIDTH || (uint16_t)y >= OLED_HEIGHT) return;
    oled_buffer[(y / 8u) * OLED_WIDTH + x] |= (uint8_t)(1u << (y & 7u));
}

OLED_EXPORT void oled_fb_draw_image(int16_t x, int16_t y, const uint8_t *bitmap,
                                   int16_t w, int16_t h, bool invert)
{
    /* Widen before adding: INT16_MAX + a bitmap dimension must not wrap. */
    int32_t right = (int32_t)x + w;
    int32_t bottom = (int32_t)y + h;
    if (!bitmap || w <= 0 || h <= 0 || x >= OLED_WIDTH ||
        y >= OLED_HEIGHT || right <= 0 || bottom <= 0) return;

    /* Clip once, keeping the original source stride. */
    int32_t left = x < 0 ? 0 : x;
    if (right > OLED_WIDTH) right = OLED_WIDTH;
    int32_t width = right - left;
    int32_t first_page = y < 0 ? -(int32_t)y / 8 : 0;
    int32_t page_count = ((int32_t)h + 7) / 8;
    int32_t end_page = (OLED_HEIGHT - (int32_t)y + 7) / 8;
    if (end_page > page_count) end_page = page_count;

    int32_t page_y = (int32_t)y + first_page * 8;
    uint32_t shift = (uint16_t)y & 7u;
    uint32_t back_shift = 8u - shift;
    uint8_t invert_mask = invert ? 0xFFu : 0u;
    uint32_t source_stride = (uint32_t)w;
    /* Maximum bitmap size is 32767 * 4096 bytes; offsets need 32 bits. */
    uint32_t source_offset = (uint32_t)first_page * source_stride + (uint32_t)(left - x);

    for (int32_t page = first_page; page < end_page;
         ++page, page_y += 8, source_offset += source_stride) {
        uint8_t mask = 0xFFu;
        if (page + 1 == page_count && (h & 7u) != 0u)
            mask = (uint8_t)((1u << (h & 7u)) - 1u);
        const uint8_t *src = bitmap + source_offset;
        const uint8_t *end = src + width;

        /* Never form a destination pointer above the framebuffer. */
        if (page_y < 0) {
            uint8_t *dst = oled_buffer + left;
            do {
                uint8_t pixels = (uint8_t)((*src++ ^ invert_mask) & mask);
                *dst++ |= (uint8_t)(pixels >> back_shift);
            } while (src != end);
        } else {
            uint8_t *dst = oled_buffer + (page_y / 8) * OLED_WIDTH + left;
            if (shift == 0) {
                do {
                    *dst++ |= (uint8_t)((*src++ ^ invert_mask) & mask);
                } while (src != end);
            } else if (page_y + 8 >= OLED_HEIGHT) {
                do {
                    uint8_t pixels = (uint8_t)((*src++ ^ invert_mask) & mask);
                    *dst++ |= (uint8_t)(pixels << shift);
                } while (src != end);
            } else {
                do {
                    uint8_t pixels = (uint8_t)((*src++ ^ invert_mask) & mask);
                    *dst |= (uint8_t)(pixels << shift);
                    dst[OLED_WIDTH] |= (uint8_t)(pixels >> back_shift);
                    ++dst;
                } while (src != end);
            }
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
