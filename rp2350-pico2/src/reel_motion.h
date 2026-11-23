#pragma once
/* Physically consistent forward transport of a decorative reel-to-reel deck.
 *
 * Forward tape travel: LEFT supply -> RIGHT take-up along the LOWER tape path.
 * Both reels rotate counter-clockwise as viewed from the front.
 * angular velocity = tape linear velocity / current winding radius.
 * Conservation of tape cross-section: r_left^2 + r_right^2 ~= constant.
 * This is an ILLUSTRATION, not audio playback telemetry from KCX.
 */
#include <stdbool.h>
#include <stdint.h>

#define REEL_MAX_WIND_RADIUS 64u
#define REEL_HUB_WIND_RADIUS 24u

typedef struct {
    uint16_t left_phase;   /* 0..65535 = one turn, positive = clockwise */
    uint16_t right_phase;
    uint16_t position;     /* simulated tape transferred, 0..65535 */
    uint8_t left_radius;
    uint8_t right_radius;
} reel_motion_t;

void reel_motion_init(reel_motion_t *m);
void reel_motion_tick(reel_motion_t *m,bool bluetooth_connected);
