#include "sim_backend.h"
#include "demo.h"
#include "ui_log.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    bool smoke = argc == 2 && strcmp(argv[1], "--smoke") == 0;
    if (argc > 1 && !smoke) {
        fprintf(stderr, "Usage: %s [--smoke]\n", argv[0]);
        return 2;
    }
    oled_fb_init();
    demo_init();
    UI_LOGI("sim", "hold keys 1-4; close the window or press Esc to exit");
    unsigned frames = 0;
    while (!sim_is_closed()) {
        demo_update();
        sim_sleep(16);
        if (smoke && ++frames >= 60) break;
    }
    sim_close();
    return 0;
}
