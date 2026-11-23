/*
 * Procedural reel-to-reel tape transport, FSA506 480x272 RGB565.
 * Forward mode: both reels CCW, supply tape drains to RIGHT take-up.
 * Decorative: link-aware only, no fictitious audio level telemetry.
 * Derived visual conventions from analog tape decks (guides, heads, capstan).
 * All geometry rasterized into 8-line tiles by the existing PIO+DMA driver.
 */
#include "reel_view.h"
#include "fsa506.h"
#include <stdint.h>
#include <stdbool.h>

#define COL(r,g,b) FSA_RGB565(r,g,b)
#define CANVAS COL(9,13,20)
#define RIM_DARK COL(38,48,57)
#define FRAME COL(38,53,66)
#define FACE COL(28,42,54)
#define BRUSH COL(34,49,60)
#define HAIRLINE COL(42,58,70)
#define STEEL COL(169,181,188)
#define METAL COL(196,208,207)
#define SHINE COL(239,241,227)
#define ALLOY_DARK COL(96,113,121)
#define SHADOW COL(4,11,16)
#define HUB COL(81,100,108)
#define RUBBER COL(24,27,28)
#define MAGNETIC COL(63,38,29)
#define TAPE COL(119,76,43)
#define TAPE_LIGHT COL(172,117,67)
#define TAPE_DARK COL(80,45,31)
#define AMBER COL(241,177,79)
#define TEAL COL(58,199,185)
#define TEXT COL(191,207,210)
#define TEXT_DARK COL(114,139,153)

#define DECK_Y 0
#define BOTTOM 216
#define CY  92
#define L_CX 122
#define R_CX 358
#define SPINDLE_R 76
#define STEPS 48u

static int clamp_i(int n,int a,int b){return n<a?a:n>b?b:n;}
static int i_sqrt(int n)
{
    int r=0;
    for(int bit=64;bit;bit>>=1){
        int t=r+bit;
        if(t*t<=n)r=t;
    }
    return r;
}
static void circle(int cx,int cy,int r,uint16_t color,int tile)
{
    if(r<=0)return;
    int first=clamp_i(cy-r,tile,tile+FSA_TILE_HEIGHT);
    int last=clamp_i(cy+r+1,tile,tile+FSA_TILE_HEIGHT);
    if(first<0)first=0;
    if(last>BOTTOM)last=BOTTOM;
    for(int y=first;y<last;++y){
        int dy=y-cy,dx=i_sqrt(r*r-dy*dy);
        fsa_rect(cx-dx,y,2*dx+1,1,color);
    }
}
static void line(int x0,int y0,int x1,int y1,int thick,uint16_t color,int tile)
{
    int lo=y0<y1?y0:y1,hi=y0>y1?y0:y1;
    if(hi+thick<tile||lo-thick>=tile+FSA_TILE_HEIGHT)return;
    int dx=x1>x0?x1-x0:x0-x1;
    int dy=y1>y0?y1-y0:y0-y1;
    int sx=x0<x1?1:-1,sy=y0<y1?1:-1;
    int err=dx-dy;
    for(;;){
        if(y0+thick>=tile && y0-thick<tile+FSA_TILE_HEIGHT)
            fsa_rect(x0-thick/2,y0-thick/2,thick,thick,color);
        if(x0==x1&&y0==y1)break;
        int e=2*err;
        if(e>-dy){err-=dy;x0+=sx;}
        if(e<dx){err+=dx;y0+=sy;}
    }
}
/* 48 steps per turn, 16-bit interpolation makes spokes smoothly rotate.
   Phase increases CLOCKWISE in screen coordinates, decreases for playback. */
static const int16_t cosine[STEPS]={1024,1015,989,946,887,812,724,623,512,392,265,134,0,-134,-265,-392,-512,-623,-724,-812,-887,-946,-989,-1015,-1024,-1015,-989,-946,-887,-812,-724,-623,-512,-392,-265,-134,0,134,265,392,512,623,724,812,887,946,989,1015};
static const int16_t sine[STEPS]={0,134,265,392,512,623,724,812,887,946,989,1015,1024,1015,989,946,887,812,724,623,512,392,265,134,0,-134,-265,-392,-512,-623,-724,-812,-887,-946,-989,-1015,-1024,-1015,-989,-946,-887,-812,-724,-623,-512,-392,-265,-134};
static int trig(uint16_t phase,bool get_sine)
{
    uint32_t p=(uint32_t)phase*STEPS;
    unsigned index=(unsigned)(p>>16);
    unsigned frac=(unsigned)(p&65535u);
    const int16_t *table=get_sine?sine:cosine;
    int a=table[index],b=table[(index+1u)%STEPS];
    return a+(int)(((int32_t)(b-a)*(int32_t)frac)>>16);
}
static void polar(int cx,int cy,int radius,uint16_t phase,int *x,int *y)
{
    *x=cx+(radius*trig(phase,false))/1024;
    *y=cy+(radius*trig(phase,true))/1024;
}
static void screws(int x,int y,int tile)
{
    circle(x+1,y+1,5,SHADOW,tile);
    circle(x,y,4,ALLOY_DARK,tile);
    circle(x-1,y-1,3,METAL,tile);
    line(x-2,y+1,x+2,y-1,1,RIM_DARK,tile);
}
static void draw_metal_reel(int x,int y,int wind,uint16_t angle,int tile)
{
    /* Silver die-cast reel flange, exposed copper-brown wound tape between
       six broad cast-aluminium spokes; concentric rings and machined hub. */
    circle(x+2,y+3,SPINDLE_R,SHADOW,tile);
    circle(x,y,SPINDLE_R,RIM_DARK,tile);
    circle(x,y,SPINDLE_R-2,STEEL,tile);
    circle(x,y,SPINDLE_R-5,SHINE,tile);
    circle(x,y,SPINDLE_R-7,ALLOY_DARK,tile);
    circle(x,y,SPINDLE_R-11,SHADOW,tile);

    /* Tape is physically behind transparent/open slots of the flange. */
    circle(x,y,wind+2,TAPE_DARK,tile);
    circle(x,y,wind,TAPE,tile);
    circle(x,y,wind-2,TAPE_LIGHT,tile);
    circle(x,y,wind-5,MAGNETIC,tile);
    circle(x,y,wind-8,TAPE,tile);

    /* Tiny uneven turn edges. These are stationary relative to tape stack,
       not an animation of individual layers sliding independently. */
    if(wind>36){
        circle(x,y,wind-13,TAPE_DARK,tile);
        circle(x,y,wind-15,TAPE,tile);
    }

    /* Cast flange spokes: each has a shadow lip + bevel + narrow polished edge. */
    for(int k=0;k<6;++k){
        uint16_t phi=(uint16_t)(angle+(uint16_t)(k*10923u));
        int x0,y0,x1,y1;
        polar(x,y,20,phi,&x0,&y0);
        polar(x,y,66,phi,&x1,&y1);
        line(x0+1,y0+1,x1+1,y1+1,19,RIM_DARK,tile);
        line(x0,y0,x1,y1,14,STEEL,tile);
        line(x0-1,y0-1,x1-1,y1-1,5,SHINE,tile);
        /* fasteners near spoke tip */
        int fx,fy;
        polar(x,y,62,phi,&fx,&fy);
        circle(fx,fy,2,RIM_DARK,tile);
    }
    /* Raised annular collar and locking hub. */
    circle(x+1,y+2,22,SHADOW,tile);
    circle(x,y,22,ALLOY_DARK,tile);
    circle(x,y,19,METAL,tile);
    circle(x,y,16,RIM_DARK,tile);
    circle(x,y,11,STEEL,tile);
    circle(x,y,8,SHINE,tile);
    circle(x,y,4,HUB,tile);
    /* Brushed metal rim highlight. */
    for(int k=30;k<=45;++k){
        uint16_t phi=(uint16_t)(k*65536u/48u);
        int ax,ay;
        polar(x,y,74,phi,&ax,&ay);
        circle(ax,ay,1,SHINE,tile);
    }
}
static void tape_path(const reel_motion_t *motion,int tile)
{
    /* Bottom run: both contact points are tangent to a COUNTER-CLOCKWISE
       rotating reel; no fake straight line across both reel centers. */
    int left_r=motion->left_radius;
    int right_r=motion->right_radius;
    int lx=L_CX-left_r/2,ly=CY+(left_r*866)/1000;
    int rx=R_CX+right_r/2,ry=CY+(right_r*866)/1000;

    line(lx,ly,184,181,5,MAGNETIC,tile);
    line(184,181,297,181,5,MAGNETIC,tile);
    line(297,181,rx,ry,5,MAGNETIC,tile);
    line(lx,ly-1,184,180,2,TAPE_LIGHT,tile);
    line(184,180,297,180,2,TAPE_LIGHT,tile);
    line(297,180,rx,ry-1,2,TAPE_LIGHT,tile);
}
static void roller(int x,int y,int tile)
{
    circle(x+2,y+2,12,SHADOW,tile);
    circle(x,y,11,ALLOY_DARK,tile);
    circle(x,y,9,STEEL,tile);
    circle(x,y,7,SHINE,tile);
    circle(x,y,4,RIM_DARK,tile);
    circle(x,y,2,STEEL,tile);
}
static void tape_heads(int tile)
{
    (void)tile; /* fsa_rect clips to the 8-line tile automatically. */
    /* Polished erase/record/playback head assembly and tape guides. */
    fsa_rect(204,183,74,25,RIM_DARK);
    fsa_rect(205,184,72,21,ALLOY_DARK);
    fsa_rect(207,185,68,2,SHINE);
    for(int h=0;h<3;++h){
        int x=210+h*22;
        fsa_rect(x,185,18,18,SHADOW);
        fsa_rect(x+1,185,16,16,STEEL);
        fsa_rect(x+3,186,12,11,METAL);
        fsa_rect(x+8,185,1,15,RIM_DARK); /* head gap */
        fsa_rect(x+3,200,12,1,SHINE);
    }
}
static void capstan(int tile)
{
    /* Rubber pressure wheel and chrome capstan right of playback head. */
    circle(286,190,12,SHADOW,tile);
    circle(286,188,10,RUBBER,tile);
    circle(286,188,7,ALLOY_DARK,tile);
    circle(286,188,4,STEEL,tile);
    circle(286,188,2,SHINE,tile);
    circle(298,181,4,STEEL,tile);
    circle(298,181,2,SHINE,tile);
}
void reel_view_draw(const reel_motion_t *motion,bool linked,bool searching,int tile)
{
    fsa_rect(0,DECK_Y,480,BOTTOM,CANVAS);
    fsa_rect(6,5,468,206,RIM_DARK);
    fsa_rect(9,8,462,200,FACE);
    fsa_rect(12,10,456,3,ALLOY_DARK);
    fsa_rect(12,204,456,3,SHADOW);
    /* Fine horizontal brush strokes give the faceplate an anodized finish. */
    for(int y=17;y<201;y+=9)fsa_rect(15,y,450,1,BRUSH);
    fsa_rect(12,174,456,2,ALLOY_DARK);
    screws(19,17,tile);screws(461,17,tile);
    screws(19,195,tile);screws(461,195,tile);

    /* Guides/tape behind the front reel flanges. */
    tape_path(motion,tile);
    draw_metal_reel(L_CX,CY,motion->left_radius,motion->left_phase,tile);
    draw_metal_reel(R_CX,CY,motion->right_radius,motion->right_phase,tile);
    roller(184,181,tile);
    roller(297,181,tile);
    tape_heads(tile);
    capstan(tile);

    fsa_rect(205,22,70,21,SHADOW);
    fsa_rect(207,24,66,17,RIM_DARK);
    if(linked){
        fsa_text(221,27,"PLAY",1,TEAL);
    }else if(searching){
        fsa_text(219,27,"SCAN",1,AMBER);
    }else{
        fsa_text(221,27,"STOP",1,TEXT_DARK);
    }
    fsa_text(81,195,"SUPPLY",1,TEXT);
    fsa_text(330,195,"TAKE UP",1,TEXT);
    /* Note: connected means BT link established; actual audio play/pause
       cannot be queried on this KCX revision. "PLAY" is cosmetic only. */
}
