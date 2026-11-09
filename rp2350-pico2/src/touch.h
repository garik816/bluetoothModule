#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {int raw_x,raw_y;bool pressed;} touch_sample_t;
typedef struct {
    uint32_t magic;
    int32_t left,right,top,bottom;
    uint32_t checksum;
} touch_calibration_t;
void touch_init(void);
bool touch_poll(touch_sample_t *sample); /* event on release */
bool touch_calibration_load(touch_calibration_t *out);
bool touch_calibration_save(const touch_calibration_t *cal);
bool touch_calibration_make(const touch_sample_t samples[5],touch_calibration_t *out);
void touch_calibration_map(const touch_calibration_t *cal,int raw_x,int raw_y,int *x,int *y);
