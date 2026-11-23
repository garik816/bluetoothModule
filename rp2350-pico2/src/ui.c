/*
 * FSA506 480x272: animated reel-to-reel transport and compact status strip.
 * Tile renderer (8 rows/tile), no framebuffer and no touch dependencies.
 * Animation is illustrative; KCX v1.7 exposes NO audio level/playback speed.
 */
#include "ui.h"
#include "fsa506.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "build_version.h"

#define C(r,g,b) FSA_RGB565(r,g,b)
#define BG      C(9,16,26)
#define DECK    C(26,38,54)
#define PANEL   C(20,31,46)
#define BRUSH   C(93,117,133)
#define EDGE    C(57,76,89)
#define METAL   C(188,200,199)
#define BRIGHT  C(229,237,226)
#define TAPE    C(131,78,42)
#define TAPEHI  C(199,132,66)
#define AMBER   C(250,177,75)
#define CYAN    C(55,214,205)
#define DIM     C(123,152,169)
#define RED     C(232,91,92)
#define GREEN   C(97,221,161)
#define NAVY    C(13,27,42)
#define BLUE    C(33,89,111)

#define STATUS_Y 216
#define STATUS_H 56
#define REEL_Y 107
#define LEFT_X  127
#define RIGHT_X 353
#define REEL_RADIUS 79

static int clamp(int n,int min,int max)
{
    return n<min?min:n>max?max:n;
}
static int isqrt_small(int n)
{
    int out=0;
    for(int step=64;step>0;step>>=1){
        int trial=out+step;
        if(trial*trial<=n)out=trial;
    }
    return out;
}
static void disc(int cx,int cy,int radius,uint16_t color,int tile_y)
{
    /* Only calculate the eight scanlines owned by the current LCD tile. */
    int from=clamp(cy-radius,tile_y,tile_y+FSA_TILE_HEIGHT);
    int to=clamp(cy+radius+1,tile_y,tile_y+FSA_TILE_HEIGHT);
    if(from<0)from=0;
    if(to>FSA_HEIGHT)to=FSA_HEIGHT;
    for(int y=from;y<to;++y){
        int d=y-cy,part=radius*radius-d*d;
        if(part<0)continue;
        int half=isqrt_small(part);
        fsa_rect(cx-half,y,2*half+1,1,color);
    }
}
static void line(int x0,int y0,int x1,int y1,int width,uint16_t color,int tile_y)
{
    /* Integer Bresenham line: fast tile rejection and no math library. */
    int miny=y0<y1?y0:y1,maxy=y0>y1?y0:y1;
    if(maxy+width<tile_y||miny>=tile_y+FSA_TILE_HEIGHT)return;
    int dx=x1>x0?x1-x0:x0-x1;
    int dy=y1>y0?y1-y0:y0-y1;
    int sx=x0<x1?1:-1,sy=y0<y1?1:-1;
    int err=dx-dy;
    for(;;){
        if(y0+width>=tile_y&&y0<tile_y+FSA_TILE_HEIGHT)
            fsa_rect(x0-width/2,y0-width/2,width,width,color);
        if(x0==x1&&y0==y1)break;
        int twice=err*2;
        if(twice>-dy){err-=dy;x0+=sx;}
        if(twice<dx){err+=dx;y0+=sy;}
    }
}
static const int8_t CS[16]={100,92,71,38,0,-38,-71,-92,-100,-92,-71,-38,0,38,71,92};
static const int8_t SN[16]={0,38,71,92,100,92,71,38,0,-38,-71,-92,-100,-92,-71,-38};

static void reel(int cx,int cy,int tape_r,int phase,int tile_y)
{
    disc(cx,cy,REEL_RADIUS,EDGE,tile_y);
    disc(cx,cy,REEL_RADIUS-3,METAL,tile_y);
    disc(cx,cy,REEL_RADIUS-8,PANEL,tile_y);
    /* Brown tape builds up on the take-up reel and drains from the other. */
    disc(cx,cy,tape_r,TAPE,tile_y);
    disc(cx,cy,tape_r-5,TAPEHI,tile_y);
    disc(cx,cy,36,DECK,tile_y);
    disc(cx,cy,29,BRUSH,tile_y);
    disc(cx,cy,26,NAVY,tile_y);
    for(int spoke=0;spoke<4;++spoke){
        int a=(phase+spoke*4)&15;
        int ex=cx+(CS[a]*58)/100;
        int ey=cy+(SN[a]*58)/100;
        line(cx,cy,ex,ey,7,METAL,tile_y);
        line(cx,cy,ex,ey,3,BRIGHT,tile_y);
    }
    disc(cx,cy,15,EDGE,tile_y);
    disc(cx,cy,10,AMBER,tile_y);
    disc(cx,cy,6,METAL,tile_y);
    disc(cx,cy,3,PANEL,tile_y);
}
static void deck(const ui_state_t *ui,int tile_y)
{
    fsa_rect(0,0,480,STATUS_Y,BG);
    fsa_rect(9,10,462,197,DECK);
    fsa_rect(12,13,456,191,PANEL);
    fsa_rect(20,20,440,2,EDGE);
    fsa_rect(19,201,443,2,EDGE);
    /* Tiny static screws. */
    disc(25,24,4,METAL,tile_y);disc(455,24,4,METAL,tile_y);
    disc(25,194,4,METAL,tile_y);disc(455,194,4,METAL,tile_y);

    bool spinning=ui->bt.connected||ui->bt.scanning||ui->busy;
    int phase=spinning?(int)(ui->anim_frame&15u):0;
    int64_t swing=(int64_t)(ui->anim_frame%1200u);
    int reel_left=spinning?65-(int)(swing/60u):59;
    int reel_right=spinning?45+(int)(swing/60u):49;
    /* During idle the reels are stationary; when active tape advances. */
    reel(LEFT_X,REEL_Y,reel_left,phase,tile_y);
    reel(RIGHT_X,REEL_Y,reel_right,(16-phase)&15,tile_y);

    /* Visible top tape run + guides below the reel flanges. */
    fsa_rect(125,27,230,2,TAPE);
    fsa_rect(126,29,228,1,AMBER);
    line(140,180,219,190,3,TAPEHI,tile_y);
    line(261,190,340,180,3,TAPEHI,tile_y);
    line(219,190,261,190,3,TAPEHI,tile_y);
    disc(218,190,9,EDGE,tile_y);
    disc(218,190,5,METAL,tile_y);
    disc(262,190,9,EDGE,tile_y);
    disc(262,190,5,METAL,tile_y);
    fsa_rect(230,184,20,13,METAL);
    fsa_rect(235,183,10,13,EDGE);

    /* Activity indicator is not a real VU meter (no audio telemetry). */
    int dots=ui->bt.connected?6:ui->busy?4:ui->bt.scanning?3:1;
    for(int i=0;i<6;++i)
        disc(213+i*11,19,3,i<dots?(spinning?CYAN:AMBER):EDGE,tile_y);
    if(ui->bt.scanning)
        fsa_text(195,42,"SEARCH",1,AMBER);
    else if(ui->busy)
        fsa_text(197,42,"CONNECT",1,AMBER);
    else if(!ui->bt.ready)
        fsa_text(206,42,"NO KCX",1,RED);
}
static void devices_overlay(const ui_state_t *ui)
{
    fsa_rect(0,0,480,STATUS_Y,BG);
    fsa_rect(8,8,464,202,DECK);
    fsa_rect(10,10,460,30,NAVY);
    char header[48];
    snprintf(header,sizeof(header),"BLUETOOTH  %u  PAGE %d",
             (unsigned)ui->bt.device_count,ui->page+1);
    fsa_text(17,18,header,1,BRIGHT);
    for(int i=0;i<4;++i){
        int index=ui->page*4+i;
        int y=44+i*40;
        if(index>=0 && (size_t)index<ui->bt.device_count){
            const kcx_device_t *d=&ui->bt.devices[index];
            bool sel=index==ui->selected;
            fsa_rect(12,y,456,37,sel?BLUE:PANEL);
            char name[52];
            snprintf(name,sizeof(name),"%d. %.27s",index+1,d->name);
            fsa_text(18,y+3,name,1,sel?BRIGHT:METAL);
            fsa_text(282,y+20,d->mac,1,sel?CYAN:DIM);
        }
    }
    if(!ui->bt.device_count)fsa_text(49,93,"НЕМАЄ ЗНАЙДЕНИХ ПРИСТРОЇВ",1,METAL);
}
static void status_bar(const ui_state_t *ui)
{
    fsa_rect(0,STATUS_Y,480,STATUS_H,NAVY);
    fsa_rect(0,STATUS_Y,480,2,CYAN);
    fsa_rect(0,243,480,1,EDGE);
    const bool connected=ui->bt.connected;
    const char *mode=ui->bt.mode_known?(ui->bt.transmitter?"TX":"RX"):"?";
    fsa_text(10,223,"BT",1,DIM);
    fsa_text(35,223,mode,1,BRIGHT);
    disc(100,229,4,connected?GREEN:RED,(STATUS_Y/FSA_TILE_HEIGHT)*FSA_TILE_HEIGHT);
    fsa_text(111,223,connected?"LINK":"NO LINK",1,connected?GREEN:AMBER);
    fsa_text(208,223,ui->auto_enabled?(ui->auto_paused?"AUTO PAUSE":"AUTO ON"):"AUTO OFF",
             1,ui->auto_enabled?CYAN:RED);
    fsa_text(333,223,ui->bt.ready?"KCX OK":"KCX OFF",1,ui->bt.ready?GREEN:RED);

    char line[59];
    if(connected && ui->bt.connected_name[0])
        snprintf(line,sizeof(line),"PLAY: %.35s",ui->bt.connected_name);
    else if(ui->bt.last_mac[0])
        snprintf(line,sizeof(line),"LAST: %.30s",ui->bt.last_mac);
    else
        snprintf(line,sizeof(line),"USB: GUI / %s",ui->bt.scanning?"SEARCH":"WAIT");
    fsa_text(10,252,line,1,BRIGHT);
    char ver[24];
    snprintf(ver,sizeof(ver),"v%s",FIRMWARE_VERSION);
    fsa_text(410,252,ver,1,DIM);
}
static void draw_tile(const ui_state_t *ui,int tile_y)
{
    if(tile_y<STATUS_Y){
        if(ui->show_list)devices_overlay(ui);
        else deck(ui,tile_y);
    }
    status_bar(ui);
}
bool ui_draw(const ui_state_t *ui)
{
    if(!ui)return false;
    for(int y=0;y<FSA_HEIGHT;y+=FSA_TILE_HEIGHT){
        fsa_begin_tile(y);
        draw_tile(ui,y);
        if(!fsa_present_tile())return false;
    }
    return true;
}
