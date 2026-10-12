#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "oled_fb.h"
#include "oled_switch.h"

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

static COLORREF pixel(HWND window, int x, int y)
{
    RECT rect;
    GetClientRect(window, &rect);
    int scale = (rect.right - 32) / OLED_WIDTH;
    if ((rect.bottom - 32) / OLED_HEIGHT < scale) scale = (rect.bottom - 32) / OLED_HEIGHT;
    if (scale < 1) scale = 1;
    int left = (rect.right - OLED_WIDTH * scale) / 2;
    int top = (rect.bottom - OLED_HEIGHT * scale) / 2;
    HDC dc = GetDC(window);
    HDC memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, rect.right, rect.bottom);
    CHECK(memory && bitmap);
    HGDIOBJ old = SelectObject(memory, bitmap);
    SendMessageW(window, WM_PRINTCLIENT, (WPARAM)memory, PRF_CLIENT);
    COLORREF result = GetPixel(memory, left + x * scale, top + y * scale);
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(window, dc);
    return result;
}

int main(int argc, char **argv)
{
    CHECK(!oled_switch_is_pressed(1));
    oled_fb_init();
    HWND window = FindWindowW(L"HeaterOLEDSimulatorWindow", NULL);
    CHECK(window != NULL);
    if (argc == 2 && strcmp(argv[1], "--exit-open") == 0) {
        oled_fb_fill_buffer(0xFF);
        oled_fb_send_buffer();
        Sleep(50);
        puts("PASS: returning from host main with the DLL window still open");
        return 0;
    }
    const COLORREF off = RGB(1, 5, 8), on = RGB(155, 207, 207);
    CHECK(pixel(window, 0, 0) == off);
    oled_fb_draw_dot(0, 0);
    oled_fb_draw_dot(127, 0);
    oled_fb_draw_dot(0, 63);
    oled_fb_draw_dot(127, 63);
    CHECK(pixel(window, 0, 0) == off);
    oled_fb_send_buffer();
    CHECK(pixel(window, 0, 0) == on && pixel(window, 127, 63) == on);
    CHECK(pixel(window, 127, 0) == on && pixel(window, 0, 63) == on);
    CHECK(pixel(window, 1, 0) == off && pixel(window, 0, 1) == off);
    oled_fb_enable(false);
    CHECK(pixel(window, 127, 63) == off);
    oled_fb_enable(true);
    CHECK(pixel(window, 127, 63) == on);
    SetWindowPos(window, NULL, 0, 0, 550, 330, SWP_NOMOVE | SWP_NOZORDER);
    CHECK(pixel(window, 0, 0) == on && pixel(window, 127, 63) == on);
    CHECK(pixel(window, 127, 0) == on && pixel(window, 0, 63) == on);

    for (unsigned key = 1; key <= 4; ++key) {
        SendMessageW(window, WM_KEYDOWN, '0' + key, 0);
        CHECK(oled_switch_is_pressed((uint8_t)key));
        SendMessageW(window, WM_KEYDOWN, '0' + key, 1L << 30);
        CHECK(oled_switch_is_pressed((uint8_t)key));
        SendMessageW(window, WM_KEYUP, '0' + key, 0);
        CHECK(!oled_switch_is_pressed((uint8_t)key));
    }
    for (unsigned key = 1; key <= 4; ++key) SendMessageW(window, WM_KEYDOWN, '0' + key, 0);
    for (uint8_t key = 1; key <= 4; ++key) CHECK(oled_switch_is_pressed(key));
    SendMessageW(window, WM_KEYUP, '2', 0);
    CHECK(oled_switch_is_pressed(1) && !oled_switch_is_pressed(2) && oled_switch_is_pressed(3) && oled_switch_is_pressed(4));
    SendMessageW(window, WM_KILLFOCUS, 0, 0);
    for (uint8_t key = 1; key <= 4; ++key) CHECK(!oled_switch_is_pressed(key));
    CHECK(!oled_switch_is_pressed(0) && !oled_switch_is_pressed(5) && !oled_switch_is_pressed(255));
    SendMessageW(window, WM_KEYDOWN, '5', 0);
    for (uint8_t key = 1; key <= 4; ++key) CHECK(!oled_switch_is_pressed(key));
    SendMessageW(window, WM_KEYDOWN, '1', 0);
    SendMessageW(window, WM_CLOSE, 0, 0);
    CHECK(!oled_switch_is_pressed(1));
    /* The library must not terminate its host when the viewer closes. */
    oled_fb_send_buffer();
    puts("PASS: DLL API, Win32 pixels/resize/power, keys/repeat/chord/focus/close");
    return 0;
}
