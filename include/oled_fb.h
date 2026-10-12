#ifndef __OLED_FB_H
#define __OLED_FB_H

#include <stdbool.h>
#include <stdint.h>
#include <stdbool.h>

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define BUFFER_LENGTH (OLED_WIDTH * ((OLED_HEIGHT + 7) / 8))

#define OLED_IS_SH1106 1

void oled_fb_send_buffer(void);
void oled_fb_fill_buffer(uint8_t item);
void oled_fb_clear_buffer(void);
void oled_fb_enable(bool en);
void oled_fb_before_fadein(void);
void oled_fb_fadeout(uint8_t step);
void oled_fb_fadein(uint8_t step);
void oled_fb_draw_dot(uint8_t x, uint8_t y);
void oled_fb_draw_image(uint8_t x,
                        uint8_t y,
                        const uint8_t* bitmap,
                        uint8_t w,
                        uint8_t h,
                        bool invert);
void oled_fb_init(void);

#endif  // __OLED_FB_H
