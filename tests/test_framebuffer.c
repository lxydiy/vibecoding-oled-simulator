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

static void test_images(void)
{
    /* Deterministic randomized reference checks, including partial pages,
     * clipping, inversion, OR compositing, and all uint8_t coordinate ranges. */
    uint32_t random = 0x384219u;
    uint8_t bitmap[255 * 32];
    uint8_t expected[BUFFER_LENGTH];
    for (unsigned trial = 0; trial < 500; ++trial) {
        unsigned values[4];
        for (unsigned j = 0; j < 4; ++j) {
            random = random * 1664525u + 1013904223u;
            values[j] = random >> 24;
        }
        unsigned x = values[0], y = values[1], w = values[2], h = values[3];
        if (trial % 2 == 0) { x %= OLED_WIDTH; y %= OLED_HEIGHT; }
        bool invert = trial % 3 == 0;
        for (unsigned j = 0; j < sizeof bitmap; ++j) bitmap[j] = (uint8_t)(j * 31u + trial);
        uint8_t fill = trial % 5 == 0 ? 0x55 : 0;
        memset(expected, fill, sizeof expected);
        oled_fb_fill_buffer(fill);
        for (unsigned py = 0; py < OLED_HEIGHT; ++py) {
            for (unsigned px = 0; px < OLED_WIDTH; ++px) {
                if (px >= x && px - x < w && py >= y && py - y < h) {
                    unsigned row = py - y, col = px - x;
                    bool set = (bitmap[(row >> 3) * w + col] & (1u << (row & 7))) != 0;
                    if (set != invert) expected[(py >> 3) * OLED_WIDTH + px] |= (uint8_t)(1u << (py & 7));
                }
            }
        }
        oled_fb_draw_image((uint8_t)x, (uint8_t)y, bitmap, (uint8_t)w, (uint8_t)h, invert);
        oled_fb_send_buffer();
        CHECK(memcmp(expected, frame.pixels, sizeof expected) == 0);
    }
    oled_fb_clear_buffer();
    oled_fb_draw_image(0, 0, NULL, 128, 64, false);
    oled_fb_send_buffer();
    expect_blank();
    /* Explicit page crossing and bottom-row regression. */
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
    puts("PASS: buffering, clipping, page layout, 500 image cases, all 256 fade steps");
    return 0;
}
