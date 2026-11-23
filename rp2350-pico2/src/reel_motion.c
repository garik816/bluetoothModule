#include "reel_motion.h"
#include <stddef.h>

#define START_POSITION    11000u  /* two visibly different tape windings */
#define POSITION_STEP     4u      /* ~45 min to end of illustrated reel */
#define TAPE_ADVANCE_PX   8u      /* same linear speed at both reel hubs */

/* Rounded non-negative integer square root (r <= 64). No libm on MCU. */
static uint8_t square_root(uint32_t n)
{
    uint32_t r=0;
    for(uint32_t bit=64u;bit>0;bit>>=1){
        uint32_t t=r+bit;
        if(t*t<=n)r=t;
    }
    return (uint8_t)r;
}
static void radii(reel_motion_t *m)
{
    uint32_t span=REEL_MAX_WIND_RADIUS*REEL_MAX_WIND_RADIUS -
                  REEL_HUB_WIND_RADIUS*REEL_HUB_WIND_RADIUS;
    uint32_t taken=span*m->position/65535u;
    m->left_radius=square_root(REEL_MAX_WIND_RADIUS*REEL_MAX_WIND_RADIUS-taken);
    m->right_radius=square_root(REEL_HUB_WIND_RADIUS*REEL_HUB_WIND_RADIUS+taken);
}
/* One turn == 65536 units. Angle radians ~= linear_pixels / winding_radius.
 * Integer approximation: 2*pi=6.2832; result is q16 turn units.
 */
static uint16_t angle_step(uint8_t radius)
{
    return (uint16_t)((65536u*TAPE_ADVANCE_PX*100u)/(628u*radius));
}
void reel_motion_init(reel_motion_t *m)
{
    if(!m)return;
    *m=(reel_motion_t){
        .left_phase=13000u,
        .right_phase=47000u,
        .position=START_POSITION
    };
    radii(m);
}
void reel_motion_tick(reel_motion_t *m,bool bluetooth_connected)
{
    if(!m || !bluetooth_connected)return;

    /* Lower tape run moves left to right. Both reels turn CCW (negative angle).
       Small supply spool -> higher angular speed, large -> lower speed.
       Calculate using OLD radius, then transfer an equal length of tape. */
    m->left_phase=(uint16_t)(m->left_phase-angle_step(m->left_radius));
    m->right_phase=(uint16_t)(m->right_phase-angle_step(m->right_radius));
    if(m->position<=65535u-POSITION_STEP)
        m->position=(uint16_t)(m->position+POSITION_STEP);
    else
        m->position=65535u;
    radii(m);
}
