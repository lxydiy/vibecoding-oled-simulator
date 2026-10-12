#include "sim_backend.h"
#include "oled_switch.h"
#include <string.h>

static sim_frame current;
static bool keys[4];
static bool closed;

void sim_publish(const uint8_t *pixels, bool enabled, uint8_t contrast)
{
    sim_lock();
    memcpy(current.pixels, pixels, sizeof current.pixels);
    current.enabled = enabled;
    current.contrast = contrast;
    sim_unlock();
}

void sim_snapshot(sim_frame *frame)
{
    sim_lock();
    *frame = current;
    sim_unlock();
}

void sim_set_key(unsigned index, bool pressed)
{
    if (index >= 4) return;
    sim_lock();
    keys[index] = pressed;
    sim_unlock();
}

void sim_reset_keys(void)
{
    sim_lock();
    memset(keys, 0, sizeof keys);
    sim_unlock();
}

OLED_EXPORT bool oled_switch_is_pressed(uint8_t switch_id)
{
    bool pressed;
    if (switch_id < 1 || switch_id > 4) return false;
    sim_lock();
    pressed = keys[switch_id - 1];
    sim_unlock();
    return pressed;
}

void sim_mark_closed(void)
{
    sim_lock();
    closed = true;
    memset(keys, 0, sizeof keys);
    sim_unlock();
}

bool sim_is_closed(void)
{
    bool result;
    sim_lock();
    result = closed;
    sim_unlock();
    return result;
}
