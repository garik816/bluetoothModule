/*
 * Ukrainian touch UI for FSA506 480x272.
 * Controls: scan, connect selected, disconnect, next page; CAL in title.
 */
#include "ui.h"
#include "fsa506.h"
#include <stdio.h>
#include <string.h>
#include "build_version.h"
#ifndef MODULE_KCX_ENABLED
#define MODULE_KCX_ENABLED 0
#endif

#define NAVY FSA_RGB565(18,36,64)
#define BLUE FSA_RGB565(19,109,204)
#define WHITE FSA_RGB565(255,255,255)
#define INK FSA_RGB565(28,38,54)
#define GRAY FSA_RGB565(220,231,240)
#define RED FSA_RGB565(180,46,55)
#define GREEN FSA_RGB565(32,145,98)

static void button(int x,int w,const char *name,uint16_t color)
{
    fsa_rect(x,226,w,43,color);
    fsa_text(x+7,238,name,1,WHITE);
}
static void draw_contents(const ui_state_t *ui)
{
    fsa_rect(0,0,480,272,WHITE);
    fsa_rect(0,0,480,43,NAVY);
    char title[88];
    snprintf(title,sizeof(title),"universal bluetooth module v.%s",FIRMWARE_VERSION);
    fsa_text(8,12,title,1,WHITE);
    fsa_text(423,11,"CAL",1,WHITE);
    if(ui->calibrating){
        const int xx[5]={45,435,435,45,240};
        const int yy[5]={70,70,210,210,136};
        fsa_text(10,45,"КАЛІБРУВАННЯ",2,BLUE);
        if(ui->cal_step>=0&&ui->cal_step<5){
            int x=xx[ui->cal_step],y=yy[ui->cal_step];
            fsa_rect(x-12,y-1,25,3,RED);
            fsa_rect(x-1,y-12,3,25,RED);
        }
        fsa_text(95,244,"ТОРКНІТЬСЯ ХРЕСТА",1,INK);
        return;
    }
#if !MODULE_KCX_ENABLED
    fsa_text(12,50,"TOUCH DIAGNOSTICS / KCX OFF",1,BLUE);
    char position[80];
    snprintf(position,sizeof(position),"RAW %d,%d  SCREEN %d,%d",
             ui->raw_x,ui->raw_y,ui->screen_x,ui->screen_y);
    fsa_text(12,85,position,1,INK);
    fsa_text(12,117,"TOUCH SCREEN TO TEST POSITION",1,INK);
    fsa_text(12,145,"CAL - TOP RIGHT CORNER",1,INK);
    fsa_text(12,173,"USB SERIAL: p c d h v",1,INK);
    fsa_rect(8,206,464,18,GRAY);
    fsa_text(13,209,ui->notice,1,INK);
    return;
#endif
    fsa_text(12,49,"KCX:",2,INK);
    const char *status=!ui->bt.ready?"НЕМАЄ ЗВ'ЯЗКУ":
        (ui->bt.mode_known&&!ui->bt.transmitter)?"РЕЖИМ RX":
        ui->bt.connected?"ПІДКЛЮЧЕНО":
        ui->bt.scanning?"ПОШУК":"ГОТОВО";
    fsa_text(85,49,status,1,ui->bt.connected?GREEN:BLUE);
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
    if(!ui->bt.device_count)fsa_text(24,105,"НАТИСНІТЬ ПОШУК",2,INK);
    fsa_rect(8,206,464,18,GRAY);
    fsa_text(13,209,ui->notice[0]?ui->notice:"ОБЕРІТЬ ПРИСТРІЙ У СПИСКУ",1,INK);
    button(8,105,"ПОШУК",BLUE);
    button(120,113,"ПІДКЛ.",GREEN);
    button(240,113,"ВІДКЛ.",RED);
    button(360,112,"ДАЛІ",NAVY);
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
