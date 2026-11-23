/* Transport physics and ONLY-WHEN-CONNECTED regression tests. */
#include "../src/reel_motion.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    reel_motion_t t;
    reel_motion_init(&t);
    assert(t.left_radius>t.right_radius);
    assert(t.left_radius<=REEL_MAX_WIND_RADIUS);
    assert(t.right_radius>=REEL_HUB_WIND_RADIUS);
    reel_motion_t old=t;
    reel_motion_tick(&t,false);
    assert(t.left_phase==old.left_phase && t.right_phase==old.right_phase);
    assert(t.position==old.position);
    assert(t.left_radius==old.left_radius && t.right_radius==old.right_radius);

    reel_motion_tick(&t,true);
    uint16_t left_angular=(uint16_t)(old.left_phase-t.left_phase);
    uint16_t right_angular=(uint16_t)(old.right_phase-t.right_phase);
    assert(left_angular>0 && right_angular>0);
    assert(left_angular<right_angular); /* smaller right take-up spins faster */
    uint32_t supply_distance=(uint32_t)left_angular*old.left_radius;
    uint32_t takeup_distance=(uint32_t)right_angular*old.right_radius;
    assert(supply_distance>0 && takeup_distance>0);
    assert(supply_distance-takeup_distance<1000u ||
           takeup_distance-supply_distance<1000u);

    for(unsigned i=0;i<15000;++i)reel_motion_tick(&t,true);
    assert(t.left_radius<=old.left_radius);
    assert(t.right_radius>=old.right_radius);
    /* Radius squared is constant apart from 1-pixel integer rounding. */
    uint32_t actual=t.left_radius*t.left_radius+t.right_radius*t.right_radius;
    uint32_t ideal=REEL_MAX_WIND_RADIUS*REEL_MAX_WIND_RADIUS+
                   REEL_HUB_WIND_RADIUS*REEL_HUB_WIND_RADIUS;
    assert(actual<=ideal && ideal-actual<220u);
    reel_motion_t stop=t;
    for(unsigned i=0;i<500;++i)reel_motion_tick(&t,false);
    assert(t.left_phase==stop.left_phase && t.right_phase==stop.right_phase);
    assert(t.position==stop.position);
    /* Reconnection restarts rotation without resetting windings. */
    reel_motion_tick(&t,true);
    assert(t.left_phase!=stop.left_phase && t.right_phase!=stop.right_phase);
    puts("RP2350 reel transport: forward CCW, variable angular speed, linked-only: PASS");
    return 0;
}
