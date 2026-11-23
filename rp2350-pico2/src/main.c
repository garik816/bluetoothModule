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
#include "ui.h"
#include "kcx_uart.h"
#include "kcx_protocol.h"
#include "build_version.h"

#define KCX_TIMEOUT_MS 5000u
#define SCAN_WINDOW_MS 12000u
#define KCX_POLL_MS 3000u

typedef enum {
    OP_IDLE=0, OP_WAIT_DISCONNECT, OP_WAIT_DELETE, OP_WAIT_ADD
} op_step_t;
static ui_state_t s_ui;
static op_step_t s_op;
static uint32_t s_op_since;
static uint32_t s_last_status;
static uint32_t s_scan_since;
static char s_target_mac[13];
static bool s_changed;
static uint32_t s_heartbeat_ms;
static const char *const s_scan_hint="ПОШУК / МОЖЛИВЕ АВТОЗЄДНАННЯ";

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
        if(s_op!=OP_IDLE){
            s_op=OP_IDLE;s_ui.busy=false;
            notice("KCX: ПОМИЛКА КОМАНДИ");
        }else{
            puts("[KCX WARN] Command rejected by module; link remains active.");
        }
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

/* A line-based USB CDC console replaces the broken resistive touch panel. */
static char s_cmd[100];
static size_t s_cmd_len;
static uint32_t s_last_heartbeat, s_last_draw;
static bool s_warned_silent;
static void show_devices(void)
{
    printf("[DEVICES] found=%u selected=%d page=%d\n",
           (unsigned)s_ui.bt.device_count,s_ui.selected+1,s_ui.page+1);
    for(size_t i=0;i<s_ui.bt.device_count;++i){
        const kcx_device_t *d=&s_ui.bt.devices[i];
        printf("[DEVICES] %2u: %.34s  MAC=%s%s\n",
               (unsigned)(i+1),d->name,d->mac,
               s_ui.selected==(int)i?" [SELECTED]":"");
    }
    if(!s_ui.bt.device_count)puts("[DEVICES] None yet. Use 'scan'.");
}
static void show_status(void)
{
    printf("[STATUS] fw=%s KCX=%s BT_MODE=%s BT_LINK=%s devices=%u busy=%d\n",
           FIRMWARE_VERSION,s_ui.bt.ready?"RESPONDING":"NO RESPONSE YET",
           !s_ui.bt.mode_known?"UNKNOWN":(s_ui.bt.transmitter?"TX":"RX"),
           !s_ui.bt.status_known?"UNKNOWN":(s_ui.bt.connected?"YES":"NO"),
           (unsigned)s_ui.bt.device_count,(int)s_ui.busy);
    if(s_ui.bt.connected&&s_ui.bt.connected_mac[0])
        printf("[BT PEER] name=%s MAC=%s\n",
               s_ui.bt.connected_name,s_ui.bt.connected_mac);
    printf("[UART] UART0 115200 8N1 GP0->KCX RX GP1<-KCX TX rx_bytes=%lu lines=%lu overflow=%lu\n",
           (unsigned long)kcx_uart_rx_count(),
           (unsigned long)kcx_uart_line_count(),
           (unsigned long)kcx_uart_overflow_count());
}
static void show_help(void)
{
    puts("[USB] help             | help / commands");
    puts("[USB] status           | KCX and UART state; sends AT+STATUS?");
    puts("[USB] ping             | send AT+");
    puts("[USB] version          | firmware version and KCX AT+GMR?");
    puts("[USB] scan             | AT+PAIR (may automatically connect)");
    puts("[USB] devices          | list devices received from KCX");
    puts("[USB] select N         | select 1-based device index");
    puts("[USB] connect CONFIRM  | ERASES KCX favorites, pairs selected MAC");
    puts("[USB] drop             | send AT+DISCON");
    puts("[USB] next             | next display page");
    puts("[USB] redraw           | redraw current LCD screen");
    puts("[USB] rawstatus        | KCX AT+BT_MODE? and AT+STATUS?");
    puts("[USB] No touch/calibration, no GP15 MODE output. Use KCX TX jumper.");
}
static void process_command(const char *line)
{
    printf("[USB] > %s\n",line);
    if(!strcmp(line,"help")||!strcmp(line,"h"))show_help();
    else if(!strcmp(line,"ping"))(void)kcx_uart_send("AT+");
    else if(!strcmp(line,"version")||!strcmp(line,"v")){
        printf("[FW] universal bluetooth module v.%s\n",FIRMWARE_VERSION);
        (void)kcx_uart_send("AT+GMR?");
    }else if(!strcmp(line,"status")){
        show_status();
        (void)kcx_uart_send("AT+STATUS?");
    }else if(!strcmp(line,"rawstatus")){
        (void)kcx_uart_send("AT+BT_MODE?");
        (void)kcx_uart_send("AT+STATUS?");
    }else if(!strcmp(line,"scan")){
        start_scan();
    }else if(!strcmp(line,"devices")||!strcmp(line,"list")){
        show_devices();
    }else if(!strncmp(line,"select ",7)){
        char *end=NULL;
        long n=strtol(line+7,&end,10);
        if(end==line+7||*end||n<1||(size_t)n>s_ui.bt.device_count){
            puts("[ERROR] Invalid index. Try 'devices'.");
        }else{
            s_ui.selected=(int)(n-1);
            s_ui.page=s_ui.selected/4;
            printf("[USB] selected #%ld MAC=%s\n",n,s_ui.bt.devices[s_ui.selected].mac);
            notice("ОБРАНО BLUETOOTH ПРИСТРІЙ");
        }
    }else if(!strcmp(line,"connect")){
        puts("[WARNING] Connecting a specific MAC deletes ALL saved KCX favorites.");
        puts("[WARNING] Confirm using: connect CONFIRM");
    }else if(!strcmp(line,"connect CONFIRM")){
        connect_selected();
    }else if(!strcmp(line,"drop")||!strcmp(line,"disconnect")){
        if(s_op!=OP_IDLE)puts("[ERROR] Busy: connection operation in progress.");
        else{(void)kcx_uart_send("AT+DISCON");notice("BLUETOOTH ВІДКЛЮЧЕННЯ");}
    }else if(!strcmp(line,"next")){
        int pages=(int)((s_ui.bt.device_count+3u)/4u);
        s_ui.page=pages?(s_ui.page+1)%pages:0;
        s_changed=true;
        printf("[UI] page=%d/%d\n",s_ui.page+1,pages?pages:1);
    }else if(!strcmp(line,"redraw")){
        s_changed=true;
        puts("[LCD] manual redraw requested");
    }else puts("[ERROR] Unknown command. Enter 'help'.");
}
static void poll_usb(void)
{
    for(int i=0;i<128;++i){
        int v=getchar_timeout_us(0);
        if(v==PICO_ERROR_TIMEOUT)break;
        char ch=(char)v;
        if(ch=='\r'||ch=='\n'){
            if(s_cmd_len){
                s_cmd[s_cmd_len]='\0';
                process_command(s_cmd);
                s_cmd_len=0;
            }
        }else if(ch==8||ch==127){
            if(s_cmd_len)s_cmd_len--;
        }else if(ch>=32&&ch<=126){
            if(s_cmd_len+1<sizeof(s_cmd))s_cmd[s_cmd_len++]=ch;
            else{s_cmd_len=0;puts("[ERROR] USB command buffer overflow");}
        }
    }
}
int main(void)
{
    stdio_init_all();
    sleep_ms(500);
    printf("\n[BOOT] universal bluetooth module v.%s\n",FIRMWARE_VERSION);
    puts("[BOOT] Raspberry Pi Pico 2 + FSA506 + KCX_BT_EMITTER v1.7");
    puts("[BOOT] touch=REMOVED; calibration=REMOVED; existing LCD PIO/DMA driver UNCHANGED");
    puts("[BOOT] KCX UART0 115200 ON: GP0 TX -> KCX RX; GP1 RX <- KCX TX");
    puts("[BOOT] KCX TX/RX mode is selected by PCB jumper. GP15 is UNUSED.");
    kcx_state_init(&s_ui.bt);
    s_ui.selected=-1;
    notice("ЗАПУСК BLUETOOTH");
    puts("[LCD] FSA506 init...");
    if(!fsa_init()){
        puts("[FATAL] FSA506 init failed.");
        for(;;)sleep_ms(2000);
    }
    puts("[LCD] init returned success. Drawing...");
    if(!ui_draw(&s_ui))puts("[LCD ERROR] first frame failed");
    else puts("[LCD] first screen rendered");
    s_last_draw=clock_ms();
    puts("[KCX] initializing UART0...");
    kcx_uart_init(on_line);
    (void)kcx_uart_send("AT+");
    sleep_ms(120);
    (void)kcx_uart_send("AT+GMR?");
    sleep_ms(120);
    (void)kcx_uart_send("AT+BT_MODE?");
    sleep_ms(120);
    (void)kcx_uart_send("AT+STATUS?");
    s_last_status=clock_ms();
    s_last_heartbeat=clock_ms();
    puts("[BOOT] ready; enter 'help' in USB COM terminal.");
    show_help();
    for(;;){
        kcx_uart_poll();
        poll_usb();
        periodic();
        uint32_t now=clock_ms();
        if((uint32_t)(now-s_last_heartbeat)>=5000u){
            printf("[HEARTBEAT] uptime=%lus fw=%s touch=OFF KCX=ON rx_bytes=%lu rx_lines=%lu\n",
                   (unsigned long)(now/1000),FIRMWARE_VERSION,
                   (unsigned long)kcx_uart_rx_count(),
                   (unsigned long)kcx_uart_line_count());
            s_last_heartbeat=now;
        }
        if(now>8000u&&!s_warned_silent&&kcx_uart_rx_count()==0){
            s_warned_silent=true;
            puts("[WARN] KCX RX silent: check 5V PGND, TX->GP1, RX<-GP0, UART levels.");
            notice("KCX НЕ ВІДПОВІДАЄ UART");
        }
        if(s_changed&&(uint32_t)(now-s_last_draw)>=120u){
            s_changed=false;
            s_last_draw=now;
            bool ok=ui_draw(&s_ui);
            printf("[LCD] draw %s\n",ok?"OK":"FAILED");
        }
        sleep_ms(5);
    }
}
