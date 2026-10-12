#define _POSIX_C_SOURCE 200809L
#include "sim_backend.h"
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t ready_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready_condition = PTHREAD_COND_INITIALIZER;
static pthread_t thread;
static bool started, ready, startup_ok, stopping;

void sim_lock(void) { pthread_mutex_lock(&state_lock); }
void sim_unlock(void) { pthread_mutex_unlock(&state_lock); }
void sim_sleep(unsigned milliseconds)
{
    struct timespec delay = {(time_t)(milliseconds / 1000u), (long)(milliseconds % 1000u) * 1000000L};
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {}
}

static void signal_ready(bool ok)
{
    pthread_mutex_lock(&ready_lock);
    startup_ok = ok;
    ready = true;
    pthread_cond_signal(&ready_condition);
    pthread_mutex_unlock(&ready_lock);
}

static void *window_thread(void *unused)
{
    Display *display = XOpenDisplay(NULL);
    Window window;
    GC gc;
    Atom close_atom;
    int width = 800, height = 416;
    bool running = true;
    Bool detectable;
    (void)unused;
    if (!display) { signal_ready(false); return NULL; }
    int screen = DefaultScreen(display);
    unsigned long black = BlackPixel(display, screen);
    window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0,
                                 (unsigned)width, (unsigned)height, 0, black, black);
    XStoreName(display, window, "OLED 128x64 | Keys 1 2 3 4 | Esc to close");
    XSelectInput(display, window, ExposureMask | StructureNotifyMask | KeyPressMask | KeyReleaseMask | FocusChangeMask);
    close_atom = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &close_atom, 1);
    XkbSetDetectableAutoRepeat(display, True, &detectable);
    gc = XCreateGC(display, window, 0, NULL);
    XMapWindow(display, window);
    XFlush(display);
    signal_ready(true);
    while (running) {
        sim_lock();
        running = !stopping;
        sim_unlock();
        while (running && XPending(display)) {
            XEvent event;
            XNextEvent(display, &event);
            switch (event.type) {
            case ClientMessage:
                if ((Atom)event.xclient.data.l[0] == close_atom) running = false;
                break;
            case ConfigureNotify:
                width = event.xconfigure.width;
                height = event.xconfigure.height;
                break;
            case FocusOut:
                sim_reset_keys();
                break;
            case KeyPress:
            case KeyRelease: {
                if (event.type == KeyRelease && !detectable && XPending(display)) {
                    XEvent next;
                    XPeekEvent(display, &next);
                    if (next.type == KeyPress && next.xkey.time == event.xkey.time &&
                        next.xkey.keycode == event.xkey.keycode) break;
                }
                KeySym key = XLookupKeysym(&event.xkey, 0);
                if (key >= XK_1 && key <= XK_4) sim_set_key((unsigned)(key - XK_1), event.type == KeyPress);
                if (key == XK_Escape && event.type == KeyPress) running = false;
                break;
            }
            default: break;
            }
        }
        if (!running) break;
        sim_frame frame;
        sim_snapshot(&frame);
        int scale = (width - 32) / OLED_WIDTH;
        if ((height - 32) / OLED_HEIGHT < scale) scale = (height - 32) / OLED_HEIGHT;
        if (scale < 1) scale = 1;
        int left = (width - OLED_WIDTH * scale) / 2;
        int top = (height - OLED_HEIGHT * scale) / 2;
        XSetForeground(display, gc, black);
        XFillRectangle(display, window, gc, 0, 0, (unsigned)width, (unsigned)height);
        if (frame.enabled && frame.contrast) {
            XColor color = {0};
            color.red = (unsigned short)(frame.contrast * 3u / 4u * 257u);
            color.green = color.blue = (unsigned short)(frame.contrast * 257u);
            color.flags = DoRed | DoGreen | DoBlue;
            bool allocated = XAllocColor(display, DefaultColormap(display, screen), &color) != 0;
            XSetForeground(display, gc, allocated ? color.pixel : WhitePixel(display, screen));
            XRectangle pixels[OLED_WIDTH * OLED_HEIGHT];
            int count = 0;
            for (int y = 0; y < OLED_HEIGHT; ++y) {
                for (int x = 0; x < OLED_WIDTH; ++x) {
                    if (frame.pixels[(y / 8) * OLED_WIDTH + x] & (1u << (y % 8))) {
                        pixels[count].x = (short)(left + x * scale);
                        pixels[count].y = (short)(top + y * scale);
                        pixels[count].width = pixels[count].height = (unsigned short)(scale >= 3 ? scale - 1 : scale);
                        ++count;
                    }
                }
            }
            if (count) XFillRectangles(display, window, gc, pixels, count);
            if (allocated) XFreeColors(display, DefaultColormap(display, screen), &color.pixel, 1, 0);
        }
        XFlush(display);
        sim_sleep(16);
    }
    sim_mark_closed();
    XFreeGC(display, gc);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return NULL;
}

void sim_close(void)
{
    if (!started) return;
    sim_lock();
    stopping = true;
    sim_unlock();
    pthread_join(thread, NULL);
    started = false;
}

void sim_open(void)
{
    if (started) return;
    if (pthread_create(&thread, NULL, window_thread, NULL) != 0) {
        fputs("OLED simulator: cannot start X11 thread\n", stderr);
        exit(EXIT_FAILURE);
    }
    started = true;
    pthread_mutex_lock(&ready_lock);
    while (!ready) pthread_cond_wait(&ready_condition, &ready_lock);
    pthread_mutex_unlock(&ready_lock);
    if (!startup_ok) {
        sim_close();
        fputs("OLED simulator: cannot open X11 display; check DISPLAY\n", stderr);
        exit(EXIT_FAILURE);
    }
    atexit(sim_close);
}
