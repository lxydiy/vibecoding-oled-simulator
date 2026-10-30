#ifndef SIM_BACKEND_H
#define SIM_BACKEND_H
#include "oled_fb.h"

#if defined(__GNUC__)
#define OLED_EXPORT __attribute__((visibility("default")))
#else
#define OLED_EXPORT
#endif

/* Internal only: never part of the public include path or shared exports. */
typedef struct {
    uint8_t pixels[BUFFER_LENGTH];
    uint8_t contrast;
    bool enabled;
} sim_frame;

void sim_open(void);
void sim_close(void);
void sim_sleep(unsigned milliseconds);
void sim_lock(void);
void sim_unlock(void);
void sim_publish(const uint8_t *pixels, bool enabled, uint8_t contrast);
void sim_snapshot(sim_frame *frame);
void sim_set_key(unsigned index, bool pressed);
void sim_reset_keys(void);
void sim_mark_closed(void);
#endif
