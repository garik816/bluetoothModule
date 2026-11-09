#pragma once
#include <stdbool.h>
typedef enum { TOUCH_RESISTIVE_DOWN=1, TOUCH_RESISTIVE_MOVE=2, TOUCH_RESISTIVE_UP=3 } touch_resistive_phase_t;
typedef void (*touch_resistive_callback_t)(touch_resistive_phase_t phase, int raw_x, int raw_y);
bool touch_resistive_start(touch_resistive_callback_t cb);
