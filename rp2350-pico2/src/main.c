/*
 * Pico 2 (RP2350) Bluetooth car radio UI for KCX_BT_EMITTER v1.7.
 * Firmware controls audio transmitter over UART; audio itself is analog
 * LINE L/R -> KCX, and is not routed through RP2350.
 */
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "fsa506.h"
#include "touch.h"
#include "ui.h"
#include "kcx_uart.h"
#include "kcx_protocol.h"

#define KCX_TIMEOUT_MS 5000u
#define SCAN_WINDOW_MS 12000u
#define KCX_POLL_MS 3000u

typedef enum {
    OP_IDLE=0, OP_WAIT_DISCONNECT, OP_WAIT_DELETE, OP_WAIT_ADD
} op_step_t;
static ui_state_t s_ui;
static touch_calibration_t s_cal;
static touch_sample_t s_cal_samples[5];
static op_step_t s_op;
static uint32_t s_op_since;
static uint32_t s_last_status;
static uint32_t s_scan_since;
static char s_target_mac[13];
static bool s_changed;
static const char *const s_scan_hint="ПОШУК МОЖЕ ПІДКЛ. АВТОМАТИЧНО";

static uint32_t clock_ms(void){return to_ms_since_boot(get_absolute_time());}
static void notice(const char *text)
{
    snprintf(s_ui.notice,sizeof(s_ui.notice),"%s",text?text:"");
    puts(s_ui.notice);
    s_changed=true;
}
static void start_scan(void)
{
    if(s_op!=OP_IDLE){notice("ЗАЙНЯТО. ЗАЧЕКАЙТЕ");return;}
    kcx_scan_clear(&s_ui.bt);
    s_ui.selected=-1;
    s_ui.page=0;
    s_scan_since=clock_ms();
    notice(s_scan_hint);
    /* KCX v1.7 AT+SCAN is unreliable. AT+PAIR starts discovery but may
       auto-connect: discovery-only mode is not guaranteed by module FW. */
    kcx_uart_send("AT+PAIR");
}
static void connect_selected(void)
{
    if(s_op!=OP_IDLE){notice("ЗАЙНЯТО. ЗАЧЕКАЙТЕ");return;}
    if(s_ui.selected<0||(size_t)s_ui.selected>=s_ui.bt.device_count){
        notice("СПОЧАТКУ ОБЕРІТЬ ПРИСТРІЙ");return;
    }
    if(!s_ui.bt.ready || (s_ui.bt.mode_known && !s_ui.bt.transmitter)){
        notice("KCX НЕ ГОТОВИЙ АБО НЕ TX");return;
    }
    snprintf(s_target_mac,sizeof(s_target_mac),"%s",
             s_ui.bt.devices[s_ui.selected].mac);
    /* This ERASES KCX's entire saved reconnect list. UI requires explicit
       row selection and a second CONNECT action before modifying it. */
    kcx_uart_send("AT+DISCON");
    s_op=OP_WAIT_DISCONNECT;
    s_op_since=clock_ms();
    s_ui.busy=true;
    notice("ОЧИЩЕННЯ СПИСКУ KCX");
}
static void on_line(const char *line)
{
    unsigned event=kcx_parse_line(&s_ui.bt,line);
    if(event && !s_ui.bt.ready)s_ui.bt.ready=true;
    if(event & KCX_EVENT_ERROR){
        if(s_op!=OP_IDLE){s_op=OP_IDLE;s_ui.busy=false;}
        notice("KCX: ПОМИЛКА КОМАНДИ");
    }
    if((event & KCX_EVENT_ERASE_ACK) && s_op==OP_WAIT_DELETE){
        char cmd[48];
        snprintf(cmd,sizeof(cmd),"AT+ADDLINKADD=%s",s_target_mac);
        kcx_uart_send(cmd);
        s_op=OP_WAIT_ADD;
        s_op_since=clock_ms();
        notice("ЗАПИС ВИБРАНОГО MAC");
    }
    if((event & KCX_EVENT_ADD_ACK) && s_op==OP_WAIT_ADD){
        /* Link to target MAC by using firmware's one-address reconnect filter */
        s_op=OP_IDLE;
        s_ui.busy=false;
        kcx_uart_send("AT+PAIR");
        notice("ПІДКЛЮЧЕННЯ ВИБРАНОГО");
    }
    if(event & KCX_EVENT_LINK){
        if(s_op==OP_IDLE)
            notice(s_ui.bt.connected?"BLUETOOTH ПІДКЛЮЧЕНО":"BLUETOOTH ВІДКЛЮЧЕНО");
        else s_changed=true;
    }
    if(event & (KCX_EVENT_DEVICE|KCX_EVENT_SCAN|KCX_EVENT_READY|
                KCX_EVENT_VERSION|KCX_EVENT_MODE))
        s_changed=true;
}
static void on_touch(touch_sample_t sample)
{
    if(s_ui.calibrating){
        if(s_ui.cal_step<0 || s_ui.cal_step>=5)s_ui.cal_step=0;
        s_cal_samples[s_ui.cal_step]=sample;
        ++s_ui.cal_step;
        if(s_ui.cal_step==5){
            if(touch_calibration_make(s_cal_samples,&s_cal)){
                s_ui.calibrating=false;
                s_ui.cal_step=0;
                notice(touch_calibration_save(&s_cal)?
                       "СЕНСОР НАЛАШТОВАНО":"КАЛІБР. OK / FLASH ПОМИЛКА");
            }else{
                s_ui.cal_step=0;
                notice("ПОМИЛКА КАЛІБРУВАННЯ");
            }
        }
        s_changed=true;
        return;
    }
    int x=0,y=0;
    touch_calibration_map(&s_cal,sample.raw_x,sample.raw_y,&x,&y);
    if(y<43 && x>=400){
        s_ui.calibrating=true;s_ui.cal_step=0;
        notice("НОВЕ КАЛІБРУВАННЯ");
        return;
    }
    if(y>=226){
        if(x<116)start_scan();
        else if(x<238)connect_selected();
        else if(x<359){
            if(s_op==OP_IDLE){
                kcx_uart_send("AT+DISCON");
                notice("КОМАНДА ВІДКЛЮЧЕННЯ");
            }else notice("ЗАЙНЯТО. ЗАЧЕКАЙТЕ");
        }else{
            int pages=(int)((s_ui.bt.device_count+3u)/4u);
            s_ui.page=pages?(s_ui.page+1)%pages:0;
            s_changed=true;
        }
    }else if(y>=83 && y<223){
        int index=s_ui.page*4+(y-83)/35;
        if(index>=0 && (size_t)index<s_ui.bt.device_count){
            s_ui.selected=index;
            notice("ОБРАНО. НАТИСНІТЬ ПІДКЛ.");
        }
    }
}
static void periodic(void)
{
    uint32_t now=clock_ms();
    if(s_op==OP_WAIT_DISCONNECT && (uint32_t)(now-s_op_since)>350){
        kcx_uart_send("AT+DELVMLINK");
        s_op=OP_WAIT_DELETE;
        s_op_since=now;
    }else if(s_op!=OP_IDLE && (uint32_t)(now-s_op_since)>KCX_TIMEOUT_MS){
        s_op=OP_IDLE;
        s_ui.busy=false;
        notice("TIMEOUT KCX - ПЕРЕВІРТЕ UART");
    }
    if(s_ui.bt.scanning && (uint32_t)(now-s_scan_since)>SCAN_WINDOW_MS){
        s_ui.bt.scanning=false;
        s_changed=true;
    }
    if(s_op==OP_IDLE && (uint32_t)(now-s_last_status)>KCX_POLL_MS){
        kcx_uart_send("AT+STATUS?");
        s_last_status=now;
    }
}
int main(void)
{
    stdio_init_all();
    sleep_ms(250);
    puts("BluetoothModule RP2350 Pico 2 / FSA506 / KCX v1.7");
    kcx_state_init(&s_ui.bt);
    s_ui.selected=-1;
    s_ui.calibrating=!touch_calibration_load(&s_cal);
    touch_init();
    if(!fsa_init()){
        puts("ERROR: FSA506 failed to initialize");
        while(1)sleep_ms(2000);
    }
    if(!ui_draw(&s_ui))puts("FSA506 first frame error");
    kcx_uart_init(on_line);
    kcx_uart_send("AT+");
    sleep_ms(100);
    kcx_uart_send("AT+GMR?");
    sleep_ms(100);
    kcx_uart_send("AT+BT_MODE?");
    sleep_ms(100);
    kcx_uart_send("AT+STATUS?");
    s_last_status=clock_ms();
    s_changed=true;
    for(;;){
        kcx_uart_poll();
        touch_sample_t tap;
        if(touch_poll(&tap))on_touch(tap);
        periodic();
        if(s_changed){
            s_changed=false;
            if(!ui_draw(&s_ui))puts("WARN FSA506 drawing failure");
        }
        sleep_ms(5);
    }
}
