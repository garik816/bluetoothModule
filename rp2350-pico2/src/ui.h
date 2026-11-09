#pragma once
#include <stdbool.h>
#include "kcx_protocol.h"
typedef struct {
    kcx_state_t bt;
    int page;
    int selected;
    bool calibrating;
    int cal_step;
    bool busy;
    char notice[72];
} ui_state_t;
bool ui_draw(const ui_state_t *ui);
