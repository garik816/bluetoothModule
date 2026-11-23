/*
 * FSA506 480x272 UI: realistic reel transport at y=0..215,
 * compact live status y=216..271, optional device-list overlay.
 * Touch is intentionally disabled; all controls come from USB CDC GUI.
 */
#include "ui.h"
#include "fsa506.h"
#include "reel_view.h"
#include <stdio.h>
#include <string.h>
#include "build_version.h"

#define C(r,g,b) FSA_RGB565(r,g,b)
#define WHITE C(229,238,235)
#define NAVY C(12,26,41)
#define DARK C(16,31,46)
#define BLUE C(33,83,111)
#define DIM C(129,151,169)
#define CYAN C(57,204,192)
#define GREEN C(94,214,156)
#define AMBER C(244,181,78)
#define RED C(225,97,97)
#define EDGE C(52,74,92)

static void list_overlay(const ui_state_t *ui)
{
    fsa_rect(0,0,480,216,NAVY);
    fsa_rect(10,9,460,201,DARK);
    fsa_rect(10,9,460,29,BLUE);
    char header[45];
    snprintf(header,sizeof(header),"BLUETOOTH %u  / PAGE %d",
             (unsigned)ui->bt.device_count,ui->page+1);
    fsa_text(20,17,header,1,WHITE);
    for(int i=0;i<4;++i){
        int index=ui->page*4+i;
        int y=43+i*40;
        if(index<0||(size_t)index>=ui->bt.device_count)break;
        const kcx_device_t *d=&ui->bt.devices[index];
        bool selected=index==ui->selected;
        fsa_rect(12,y,455,37,selected?BLUE:NAVY);
        char name[58];
        snprintf(name,sizeof(name),"%d. %.28s",index+1,d->name);
        fsa_text(18,y+3,name,1,selected?WHITE:DIM);
        fsa_text(287,y+20,d->mac,1,selected?CYAN:WHITE);
    }
    if(ui->bt.device_count==0)fsa_text(40,98,"СПИСОК ПОРОЖНІЙ. GUI: ПОШУК",1,WHITE);
}
static void status_strip(const ui_state_t *ui)
{
    fsa_rect(0,216,480,56,NAVY);
    fsa_rect(0,216,480,2,CYAN);
    fsa_rect(0,242,480,1,EDGE);
    bool connected=ui->bt.connected;
    fsa_rect(10,226,5,5,connected?GREEN:RED);
    fsa_text(21,223,connected?"BT LINK":"NO LINK",1,connected?GREEN:AMBER);
    fsa_text(125,223,ui->bt.mode_known?(ui->bt.transmitter?"TX":"RX"):"?",1,WHITE);
    fsa_text(175,223,ui->auto_enabled?(ui->auto_paused?"AUTO PAUSE":"AUTO ON"):"AUTO OFF",
             1,ui->auto_enabled?CYAN:DIM);
    fsa_text(332,223,ui->bt.ready?"KCX ONLINE":"KCX OFF",1,ui->bt.ready?GREEN:RED);

    char line[59];
    if(connected && ui->bt.connected_name[0])
        snprintf(line,sizeof(line),"LINKED: %.32s",ui->bt.connected_name);
    else if(ui->bt.last_mac[0])
        snprintf(line,sizeof(line),"LAST MAC: %.20s",ui->bt.last_mac);
    else if(ui->bt.scanning)
        snprintf(line,sizeof(line),"BLUETOOTH: SCANNING...");
    else
        snprintf(line,sizeof(line),"WAITING FOR BLUETOOTH...");
    fsa_text(10,252,line,1,WHITE);
    char version[24];
    snprintf(version,sizeof(version),"v%s",FIRMWARE_VERSION);
    fsa_text(408,252,version,1,DIM);
}
bool ui_draw(const ui_state_t *ui)
{
    if(!ui)return false;
    for(int y=0;y<FSA_HEIGHT;y+=FSA_TILE_HEIGHT){
        fsa_begin_tile(y);
        if(y<216){
            if(ui->show_list)list_overlay(ui);
            else reel_view_draw(&ui->reels,ui->bt.connected,
                                ui->bt.scanning||ui->busy,y);
        }
        status_strip(ui);
        if(!fsa_present_tile())return false;
    }
    return true;
}
