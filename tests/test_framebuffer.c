#include "oled_fb.h"
#include "sim_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)
static sim_frame frame;
static unsigned sends, sleeps, opens;
void sim_open(void) { ++opens; }
void sim_sleep(unsigned ms) { CHECK(ms == 4); ++sleeps; }
void sim_publish(const uint8_t *pixels, bool en, uint8_t contrast)
{
    memcpy(frame.pixels, pixels, BUFFER_LENGTH);
    frame.enabled = en;
    frame.contrast = contrast;
    ++sends;
}

static void expect_blank(void)
{
    for (unsigned i = 0; i < BUFFER_LENGTH; ++i) CHECK(frame.pixels[i] == 0);
}

static unsigned image_cases;

static void check_image(int16_t x, int16_t y, const uint8_t *bitmap,
                        int16_t w, int16_t h, bool invert, uint8_t fill)
{
    uint8_t expected[BUFFER_LENGTH];
    memset(expected, fill, sizeof expected);
    oled_fb_fill_buffer(fill);
    /* Independent per-pixel oracle; production drawing only visits bytes. */
    for (int32_t py = 0; py < OLED_HEIGHT; ++py) {
        for (int32_t px = 0; px < OLED_WIDTH; ++px) {
            int32_t row = py - y, col = px - x;
            if (bitmap && row >= 0 && row < h && col >= 0 && col < w) {
                bool set = (bitmap[(row / 8) * w + col] & (1u << (row & 7))) != 0;
                if (set != invert)
                    expected[(py / 8) * OLED_WIDTH + px] |= (uint8_t)(1u << (py & 7));
            }
        }
    }
    oled_fb_draw_image(x, y, bitmap, w, h, invert);
    oled_fb_send_buffer();
    if (memcmp(expected, frame.pixels, sizeof expected) != 0) {
        fprintf(stderr, "Image mismatch: x=%d y=%d w=%d h=%d invert=%d\n",
                x, y, w, h, invert);
        exit(1);
    }
    CHECK(memcmp(expected, oled_buffer, sizeof expected) == 0);
    ++image_cases;
}

static void test_images(void)
{
    uint32_t random = 0x384219u;
    for (unsigned trial = 0; trial < 2000; ++trial) {
        int16_t values[4];
        for (unsigned j = 0; j < 4; ++j) {
            random = random * 1664525u + 1013904223u;
            values[j] = (int16_t)(random >> 23);
        }
        int16_t x = (int16_t)(values[0] - 256), y = (int16_t)(values[1] - 256);
        int16_t w = values[2], h = values[3];
        if (trial % 4 == 0) {
            x = (int16_t)((int32_t)(random & 0xFFFFu) - 32768);
            y = (int16_t)((int32_t)(random >> 16) - 32768);
        }
        bool invert = trial % 3 == 0;
        size_t size = (size_t)w * (((size_t)h + 7) / 8);
        uint8_t *bitmap = malloc(size ? size : 1);
        CHECK(bitmap != NULL);
        for (size_t j = 0; j < size; ++j) bitmap[j] = (uint8_t)(j * 31u + trial);
        uint8_t fill = trial % 5 == 0 ? 0x55 : 0;
        check_image(x, y, bitmap, w, h, invert, fill);
        free(bitmap);
    }
    /* Every vertical alignment and partial-page height, at all four edges. */
    const int16_t xs[] = {-17, -8, -1, 0, 1, 120, 127, 128};
    const int16_t ys[] = {-17, -8, 0, 56, 64};
    uint8_t small[17 * 3];
    for (unsigned i = 0; i < sizeof small; ++i) small[i] = (uint8_t)(i * 31u + 0xA5u);
    for (unsigned xi = 0; xi < sizeof xs / sizeof xs[0]; ++xi) {
        for (unsigned yi = 0; yi < sizeof ys / sizeof ys[0]; ++yi) {
            for (int16_t offset = 0; offset < 8; ++offset) {
                for (int16_t h = 1; h <= 17; ++h) {
                    check_image(xs[xi], (int16_t)(ys[yi] + offset), small, 17, h, false, 0);
                    check_image(xs[xi], (int16_t)(ys[yi] + offset), small, 17, h, true, 0x55);
                }
            }
        }
    }
    const int16_t invalid[] = {INT16_MIN, -1, 0};
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        check_image(0, 0, small, invalid[i], 8, true, 0x55);
        check_image(0, 0, small, 17, invalid[i], true, 0x55);
    }
    check_image(0, 0, NULL, 128, 64, false, 0x55);

    /* Exact allocation exercises source strides and offsets above 65535. */
    size_t large_size = (size_t)INT16_MAX * (((size_t)INT16_MAX + 7) / 8);
    uint8_t *large = malloc(large_size);
    CHECK(large != NULL);
    for (size_t i = 0; i < large_size; ++i) large[i] = (uint8_t)(i * 37u + i / 32767u);
    const int16_t edges[] = {INT16_MIN, INT16_MIN + 1, INT16_MIN + 2, -255, -1, 0, 63, 127, 255, INT16_MAX};
    for (unsigned xi = 0; xi < sizeof edges / sizeof edges[0]; ++xi) {
        for (unsigned yi = 0; yi < sizeof edges / sizeof edges[0]; ++yi) {
            check_image(edges[xi], edges[yi], large, INT16_MAX, INT16_MAX, false, 0);
            check_image(edges[xi], edges[yi], large, INT16_MAX, INT16_MAX, true, 0x55);
        }
    }
    free(large);
    /* Explicit page crossing and bottom-row regression. */
    oled_fb_clear_buffer();
    const uint8_t all[] = {0xFF, 0xFF};
    oled_fb_draw_image(127, 63, all, 2, 1, false);
    oled_fb_send_buffer();
    CHECK(frame.pixels[1023] == 0x80);
    for (unsigned i = 0; i < 1023; ++i) CHECK(frame.pixels[i] == 0);
}

int main(void)
{
    oled_fb_init();
    CHECK(opens == 1 && frame.enabled && frame.contrast == 0xCF);
    expect_blank();
    unsigned old_sends = sends;
    oled_fb_draw_dot(0, 0);
    oled_fb_draw_dot(127, 63);
    oled_fb_draw_dot(255, 255);
    oled_fb_draw_dot(-1, 0);
    oled_fb_draw_dot(0, -1);
    oled_fb_draw_dot(INT16_MIN, INT16_MAX);
    oled_fb_draw_dot(INT16_MAX, INT16_MIN);
    CHECK(sends == old_sends);
    expect_blank();
    oled_fb_send_buffer();
    CHECK(frame.pixels[0] == 1 && frame.pixels[1023] == 0x80);
    oled_fb_clear_buffer();
    oled_fb_enable(false);
    CHECK(!frame.enabled && frame.pixels[0] == 1);
    oled_fb_enable(true);
    CHECK(frame.enabled && frame.pixels[1023] == 0x80);
    oled_fb_send_buffer();
    expect_blank();
    oled_buffer[0] = 0xA5;
    expect_blank();
    oled_fb_send_buffer();
    CHECK(frame.pixels[0] == 0xA5);
    test_images();
    for (unsigned step = 0; step <= 255; ++step) {
        oled_fb_before_fadein();
        CHECK(!frame.enabled);
        oled_fb_fadein((uint8_t)step);
        CHECK(frame.enabled && frame.contrast == 0xCF);
        CHECK(frame.pixels[1023] == 0x80);
        oled_fb_fadeout((uint8_t)step);
        CHECK(!frame.enabled && frame.contrast == 0);
    }
    CHECK(sleeps > 0);
    oled_fb_init();
    CHECK(frame.enabled && frame.contrast == 0xCF);
    expect_blank();
    printf("PASS: buffering, int16 clipping, page layout, %u image cases, all 256 fade steps\n", image_cases);
    return 0;
}
