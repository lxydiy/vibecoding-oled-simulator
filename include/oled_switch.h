#ifndef OLED_SWITCH_H
#define OLED_SWITCH_H
#include <stdbool.h>
#include <stdint.h>

/* switch_id: 1..4; true = held down, false = released/invalid/unfocused.
 * Safe to query from any thread. No edge events, toggling, or debounce. */
bool oled_switch_is_pressed(uint8_t switch_id);
#endif
