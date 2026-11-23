/* FSA506 status/list display; Bluetooth control over USB CDC, no Touch. */
#include "ui.h"
#include "fsa506.h"
#include <stdio.h>
#include <string.h>
#include "build_version.h"

#define NAVY FSA_RGB565(18,36,64)
#define BLUE FSA_RGB565(19,109,204)
#define WHITE FSA_RGB565(255,255,255)
#define INK FSA_RGB565(28,38,54)
#define GRAY FSA_RGB565(220,231,240)
#define RED FSA_RGB565(180,46,55)
#define GREEN FSA_RGB565(32,145,98)

static void draw_contents(const ui_state_t *ui)
{
    fsa_rect(0,0,480,272,WHITE);
    fsa_rect(0,0,480,43,NAVY);
    char title[88];
    snprintf(title,sizeof(title),"universal bluetooth module v.%s",FIRMWARE_VERSION);
    fsa_text(8,12,title,1,WHITE);
    fsa_text(12,49,"KCX:",2,INK);
    const char *status=!ui->bt.ready?"НЕМАЄ ЗВ'ЯЗКУ":
        (ui->bt.mode_known&&!ui->bt.transmitter)?"РЕЖИМ RX":
        ui->bt.connected?"ПІДКЛЮЧЕНО":
        ui->bt.scanning?"ПОШУК":"ГОТОВО";
    fsa_text(85,49,status,1,ui->bt.connected?GREEN:BLUE);
    if(ui->bt.connected&&ui->bt.connected_name[0])
        fsa_text(12,65,ui->bt.connected_name,1,GREEN);
    if(ui->busy)fsa_text(343,53,"ЗАЧЕКАЙТЕ",1,RED);
    fsa_rect(8,78,464,1,GRAY);
    for(int j=0;j<4;++j){
        int index=ui->page*4+j,y=83+j*35;
        if(index<0||(size_t)index>=ui->bt.device_count)break;
        const kcx_device_t *d=&ui->bt.devices[index];
        bool selected=index==ui->selected;
        if(selected)fsa_rect(9,y,463,33,BLUE);
        char name[70];
        snprintf(name,sizeof(name),"%d. %.19s",index+1,d->name);
        fsa_text(12,y+2,name,1,selected?WHITE:INK);
        fsa_text(276,y+17,d->mac,1,selected?WHITE:BLUE);
    }
    if(!ui->bt.device_count)fsa_text(24,105,"СПИСОК ПОРОЖНІЙ. USB: scan",1,INK);
    fsa_rect(8,206,464,18,GRAY);
    fsa_text(13,209,ui->notice[0]?ui->notice:"КЕРУВАННЯ ЧЕРЕЗ USB",1,INK);
    fsa_rect(0,226,480,46,NAVY);
    fsa_text(11,235,"USB: help  scan  devices  select N",1,WHITE);
    fsa_text(11,253,"connect CONFIRM  drop  redraw",1,WHITE);
    fsa_text(349,253,!ui->auto_enabled?"AUTO:OFF":
             ui->auto_paused?"AUTO:PAUSE":"AUTO:ON",1,WHITE);
}
bool ui_draw(const ui_state_t *ui)
{
    if(!ui)return false;
    for(int y=0;y<FSA_HEIGHT;y+=FSA_TILE_HEIGHT){
        fsa_begin_tile(y);
        draw_contents(ui);
        if(!fsa_present_tile())return false;
    }
    return true;
}
