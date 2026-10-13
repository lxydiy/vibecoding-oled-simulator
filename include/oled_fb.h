#ifndef __OLED_FB_H
#define __OLED_FB_H

#include <stdbool.h>
#include <stdint.h>

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define BUFFER_LENGTH (OLED_WIDTH * ((OLED_HEIGHT + 7) / 8))

#define OLED_IS_SH1106 1

/* Page-major framebuffer: [y / 8 * OLED_WIDTH + x], bit (y & 7).
 * Direct writes are displayed by oled_fb_send_buffer(). */
#if defined(_WIN32) && defined(OLED_SIM_SHARED)
__declspec(dllimport) extern uint8_t oled_buffer[BUFFER_LENGTH];
#else
extern uint8_t oled_buffer[BUFFER_LENGTH];
#endif

void oled_fb_send_buffer(void);
void oled_fb_fill_buffer(uint8_t item);
void oled_fb_clear_buffer(void);
void oled_fb_enable(bool en);
void oled_fb_before_fadein(void);
void oled_fb_fadeout(uint8_t step);
void oled_fb_fadein(uint8_t step);
/* Signed pixel coordinates are clipped to the display. */
void oled_fb_draw_dot(int16_t x, int16_t y);
/* Page-major bitmap, bit 0 at the top. Supply w * ceil(h / 8) bytes.
 * Pixels are ORed into the framebuffer; invert applies only within w x h.
 * Null bitmaps and nonpositive dimensions are ignored. */
void oled_fb_draw_image(int16_t x,
                        int16_t y,
                        const uint8_t* bitmap,
                        int16_t w,
                        int16_t h,
                        bool invert);
void oled_fb_init(void);

#endif  // __OLED_FB_H
