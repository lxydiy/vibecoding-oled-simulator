#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include "sim_backend.h"

static SRWLOCK state_lock = SRWLOCK_INIT;
static HANDLE thread_handle, ready_event;
static HWND window_handle;
static bool startup_ok;

void sim_lock(void) { AcquireSRWLockExclusive(&state_lock); }
void sim_unlock(void) { ReleaseSRWLockExclusive(&state_lock); }
void sim_sleep(unsigned milliseconds) { Sleep(milliseconds); }

static void paint(HWND window, HDC dc)
{
    RECT client, screen;
    sim_frame frame;
    int scale, left, top;
    GetClientRect(window, &client);
    sim_snapshot(&frame);
    scale = (client.right - 32) / OLED_WIDTH;
    if ((client.bottom - 32) / OLED_HEIGHT < scale)
        scale = (client.bottom - 32) / OLED_HEIGHT;
    if (scale < 1) scale = 1;
    left = (client.right - OLED_WIDTH * scale) / 2;
    top = (client.bottom - OLED_HEIGHT * scale) / 2;
    SetDCBrushColor(dc, RGB(12, 17, 22));
    FillRect(dc, &client, (HBRUSH)GetStockObject(DC_BRUSH));
    SetRect(&screen, left, top, left + OLED_WIDTH * scale, top + OLED_HEIGHT * scale);
    SetDCBrushColor(dc, RGB(1, 5, 8));
    FillRect(dc, &screen, (HBRUSH)GetStockObject(DC_BRUSH));
    if (!frame.enabled || !frame.contrast) return;
    SetDCBrushColor(dc, RGB(frame.contrast * 3 / 4, frame.contrast, frame.contrast));
    for (int y = 0; y < OLED_HEIGHT; ++y) {
        for (int x = 0; x < OLED_WIDTH; ++x) {
            if (frame.pixels[(y / 8) * OLED_WIDTH + x] & (1u << (y % 8))) {
                RECT pixel;
                int size = scale >= 3 ? scale - 1 : scale;
                SetRect(&pixel, left + x * scale, top + y * scale,
                        left + x * scale + size, top + y * scale + size);
                FillRect(dc, &pixel, (HBRUSH)GetStockObject(DC_BRUSH));
            }
        }
    }
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(window, &ps);
        /* Double buffering avoids flicker during resizing and frame changes. */
        RECT rect;
        GetClientRect(window, &rect);
        HDC memory = CreateCompatibleDC(dc);
        HBITMAP bitmap = CreateCompatibleBitmap(dc, rect.right, rect.bottom);
        if (memory && bitmap) {
            HGDIOBJ old = SelectObject(memory, bitmap);
            paint(window, memory);
            BitBlt(dc, 0, 0, rect.right, rect.bottom, memory, 0, 0, SRCCOPY);
            SelectObject(memory, old);
        } else {
            paint(window, dc);
        }
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        EndPaint(window, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        paint(window, (HDC)wp);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
    case WM_SIZE:
        InvalidateRect(window, NULL, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *limits = (MINMAXINFO *)lp;
        limits->ptMinTrackSize.x = 180;
        limits->ptMinTrackSize.y = 140;
        return 0;
    }
    case WM_KEYDOWN:
    case WM_KEYUP:
        if (wp >= '1' && wp <= '4') sim_set_key((unsigned)(wp - '1'), message == WM_KEYDOWN);
        if (wp == VK_ESCAPE && message == WM_KEYDOWN) DestroyWindow(window);
        return 0;
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        if (wp >= '1' && wp <= '4') sim_set_key((unsigned)(wp - '1'), message == WM_SYSKEYDOWN);
        break;
    case WM_KILLFOCUS:
        sim_reset_keys();
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, 1);
        sim_lock();
        window_handle = NULL;
        sim_unlock();
        sim_mark_closed();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

static unsigned __stdcall window_thread(void *unused)
{
    const wchar_t *class_name = L"HeaterOLEDSimulatorWindow";
    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSW wc = {0};
    RECT rect = {0, 0, 800, 416};
    HWND window;
    MSG message;
    (void)unused;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = class_name;
    if (!RegisterClassW(&wc)) {
        SetEvent(ready_event);
        return 1;
    }
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    window = CreateWindowExW(0, class_name, L"OLED 128x64 | Keys 1 2 3 4 | Esc to close",
                            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            rect.right - rect.left, rect.bottom - rect.top,
                            NULL, NULL, instance, NULL);
    if (window && SetTimer(window, 1, 16, NULL)) {
        sim_lock();
        window_handle = window;
        sim_unlock();
        ShowWindow(window, SW_SHOW);
        startup_ok = true;
    } else if (window) {
        DestroyWindow(window);
    }
    SetEvent(ready_event);
    if (startup_ok) {
        int result;
        while ((result = (int)GetMessageW(&message, NULL, 0, 0)) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (result < 0) DestroyWindow(window);
    }
    UnregisterClassW(class_name, instance);
    return startup_ok ? 0u : 1u;
}

void sim_close(void)
{
    HWND window;
    if (!thread_handle) return;
    /* During DLL process teardown Windows may have terminated other threads.
     * Never take a lock that such a thread could have held when it exited. */
    if (WaitForSingleObject(thread_handle, 0) != WAIT_OBJECT_0) {
        sim_lock();
        window = window_handle;
        sim_unlock();
        if (window) PostMessageW(window, WM_CLOSE, 0, 0);
        WaitForSingleObject(thread_handle, INFINITE);
    }
    CloseHandle(thread_handle);
    thread_handle = NULL;
}

void sim_open(void)
{
    if (thread_handle) return;
    ready_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (ready_event)
        thread_handle = (HANDLE)_beginthreadex(NULL, 0, window_thread, NULL, 0, NULL);
    if (!thread_handle) {
        if (ready_event) CloseHandle(ready_event);
        fputs("OLED simulator: cannot start Win32 window thread\n", stderr);
        exit(EXIT_FAILURE);
    }
    WaitForSingleObject(ready_event, INFINITE);
    CloseHandle(ready_event);
    if (!startup_ok) {
        sim_close();
        fputs("OLED simulator: cannot create Win32 window\n", stderr);
        exit(EXIT_FAILURE);
    }
    atexit(sim_close);
}
