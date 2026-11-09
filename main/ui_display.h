#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "kcx_protocol.h"
typedef struct {
    kcx_state_t bt;
    int page;
    int selected;     /* absolute index, -1 if nothing selected */
    bool calibrating;
    int cal_step;
    bool calibrated;
    char notice[64];
} ui_view_t;
bool ui_display_init(void);
bool ui_display_draw(const ui_view_t *view);
