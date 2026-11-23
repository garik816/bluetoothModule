#pragma once
#include <stdbool.h>
#include "kcx_protocol.h"
typedef struct {
    kcx_state_t bt;
    int page;
    int selected;
    bool busy;
    bool auto_enabled;
    bool auto_paused;
    char notice[72];
} ui_state_t;
bool ui_draw(const ui_state_t *ui);
