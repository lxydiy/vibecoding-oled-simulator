#ifndef OLED_SIM_H
#define OLED_SIM_H
#include <stdbool.h>

/* Safe to query from any thread. Once the window closes, remains true. */
bool oled_sim_is_closed(void);
#endif
