/*
 * BluetoothModule: ESP32-S3 + FSA506 480x272 + KCX_BT_EMITTER v1.7
 * Main UI (one FreeRTOS task), UART RX (separate task), resistive touch (task).
 * Original FSA506/touch electrical timings sourced from Silvus Configurator.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "kcx_protocol.h"
#include "kcx_uart.h"
#include "touch_resistive.h"
#include "ui_display.h"

#define SCREEN_W 480
#define SCREEN_H 272
#define CAL_COUNT 5
#define CAL_MAGIC 0x46534154u
#define CAL_VERSION 1
#define COMMAND_TIMEOUT_US 5000000LL
#define APP_EVENTS_CAPACITY 64

static const char *TAG="bt_audio";
static const int s_cal_x[5]={45,435,435,45,240};
static const int s_cal_y[5]={70,70,210,210,136};

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    int32_t raw_left,raw_right,raw_top,raw_bottom;
} cal_t;

typedef enum { EVENT_TOUCH_UP=1,EVENT_KCX_LINE=2 } event_kind_t;
typedef struct {
    event_kind_t kind;
    int x,y;
    char line[160];
} event_t;

typedef enum {
    ACTION_IDLE=0,
    ACTION_WAIT_CLEAR=1,
    ACTION_WAIT_ADD=2
} action_stage_t;

static ui_view_t s_ui;
static cal_t s_cal;
static int s_samples[CAL_COUNT][2];
static QueueHandle_t s_events;
static action_stage_t s_stage;
static int64_t s_deadline;
static char s_target_mac[13];

static void notice(const char *message)
{
    snprintf(s_ui.notice,sizeof(s_ui.notice),"%s",message ? message : "");
    ESP_LOGI(TAG,"%s",s_ui.notice);
}
static bool valid_cal(const cal_t *c)
{
    if(c->magic!=CAL_MAGIC || c->version!=CAL_VERSION) return false;
    if(c->raw_left<0 || c->raw_left>4095 || c->raw_right<0 || c->raw_right>4095 ||
       c->raw_top<0 || c->raw_top>4095 || c->raw_bottom<0 || c->raw_bottom>4095) return false;
    return abs(c->raw_right-c->raw_left)>=700 &&
           abs(c->raw_bottom-c->raw_top)>=700;
}
static bool load_cal(void)
{
    nvs_handle_t handle;
    if(nvs_open("fsa_touch",NVS_READONLY,&handle)!=ESP_OK) return false;
    size_t size=sizeof(s_cal);
    esp_err_t e=nvs_get_blob(handle,"cal_v1",&s_cal,&size);
    nvs_close(handle);
    return e==ESP_OK && size==sizeof(s_cal) && valid_cal(&s_cal);
}
static bool save_cal(void)
{
    nvs_handle_t handle;
    if(nvs_open("fsa_touch",NVS_READWRITE,&handle)!=ESP_OK) return false;
    esp_err_t e=nvs_set_blob(handle,"cal_v1",&s_cal,sizeof(s_cal));
    if(e==ESP_OK) e=nvs_commit(handle);
    nvs_close(handle);
    return e==ESP_OK;
}
static int clamp_i(int x,int lo,int hi){return x<lo?lo:(x>hi?hi:x);}
static int map_axis(int raw,int from,int to,int start,int stop,int limit)
{
    if(abs(to-from)<100) return 0;
    return clamp_i(start + (int)((int64_t)(raw-from)*(stop-start)/(to-from)),0,limit);
}
static void map_touch(int raw_x,int raw_y,int *x,int *y)
{
    *x=map_axis(raw_x,s_cal.raw_left,s_cal.raw_right,s_cal_x[0],s_cal_x[1],SCREEN_W-1);
    *y=map_axis(raw_y,s_cal.raw_top,s_cal.raw_bottom,s_cal_y[0],s_cal_y[3],SCREEN_H-1);
}
static bool calibrate_finish(void)
{
    s_cal=(cal_t){
        .magic=CAL_MAGIC,.version=CAL_VERSION,
        .raw_left=(s_samples[0][0]+s_samples[3][0])/2,
        .raw_right=(s_samples[1][0]+s_samples[2][0])/2,
        .raw_top=(s_samples[0][1]+s_samples[1][1])/2,
        .raw_bottom=(s_samples[2][1]+s_samples[3][1])/2,
    };
    if(!valid_cal(&s_cal)) return false;
    int x,y; map_touch(s_samples[4][0],s_samples[4][1],&x,&y);
    return abs(x-s_cal_x[4])<=65 && abs(y-s_cal_y[4])<=50;
}
static void touch_callback(touch_resistive_phase_t phase,int raw_x,int raw_y)
{
    if(phase!=TOUCH_RESISTIVE_UP || !s_events) return;
    event_t item={.kind=EVENT_TOUCH_UP,.x=raw_x,.y=raw_y};
    (void)xQueueSend(s_events,&item,0);
}
static void kcx_callback(const char *line)
{
    if(!s_events) return;
    event_t item={.kind=EVENT_KCX_LINE};
    snprintf(item.line,sizeof(item.line),"%s",line);
    (void)xQueueSend(s_events,&item,pdMS_TO_TICKS(25));
}
static void start_scan(void)
{
    if(s_stage!=ACTION_IDLE){notice("BUSY - WAIT");return;}
    kcx_scan_clear(&s_ui.bt);
    s_ui.page=0;
    s_ui.selected=-1;
    notice("Searching / pairing...");
    /* KCX 1.7 AT+SCAN is unreliable on some firmware revisions.
       AT+PAIR starts discovery but may connect automatically. */
    kcx_uart_command("AT+PAIR");
}
static void start_select(void)
{
    if(s_stage!=ACTION_IDLE){notice("BUSY - WAIT");return;}
    if(s_ui.selected<0 || (size_t)s_ui.selected>=s_ui.bt.device_count){
        notice("CHOOSE DEVICE FIRST");return;
    }
    if(!s_ui.bt.ready || (s_ui.bt.mode_known && !s_ui.bt.transmitter)){
        notice("KCX NOT READY / RX MODE");return;
    }
    snprintf(s_target_mac,sizeof(s_target_mac),"%s",s_ui.bt.devices[s_ui.selected].mac);
    /* KCX 1.7 exposes a stored address allowlist, not a documented direct
       CONNECT-by-MAC command. Clearing it is intentional and mandatory
       here; this overwrites any previous module favourites. */
    notice("CLEARING KCX FAVORITES");
    kcx_uart_command("AT+DISCON");
    vTaskDelay(pdMS_TO_TICKS(120));
    kcx_uart_command("AT+DELVMLINK");
    s_stage=ACTION_WAIT_CLEAR;
    s_deadline=esp_timer_get_time()+COMMAND_TIMEOUT_US;
}
static bool handle_kcx(const char *line)
{
    unsigned e=kcx_parse_line(&s_ui.bt,line);
    if((e & KCX_EVENT_ERASE_ACK) && s_stage==ACTION_WAIT_CLEAR){
        char cmd[64];
        snprintf(cmd,sizeof(cmd),"AT+ADDLINKADD=%s",s_target_mac);
        kcx_uart_command(cmd);
        s_stage=ACTION_WAIT_ADD;
        s_deadline=esp_timer_get_time()+COMMAND_TIMEOUT_US;
        notice("SAVING SELECTED MAC");
        return true;
    }
    if((e & KCX_EVENT_ADD_ACK) && s_stage==ACTION_WAIT_ADD){
        kcx_uart_command("AT+PAIR");
        s_stage=ACTION_IDLE;
        notice("CONNECTING SELECTED");
        return true;
    }
    if((e & KCX_EVENT_ERROR) && s_stage!=ACTION_IDLE){
        s_stage=ACTION_IDLE;
        notice("KCX COMMAND ERROR");
        return true;
    }
    if(e & KCX_EVENT_ERROR) notice("KCX RETURNED ERROR");
    if(e & KCX_EVENT_LINK) notice(s_ui.bt.connected?"BLUETOOTH CONNECTED":"BLUETOOTH DISCONNECTED");
    return e!=KCX_EVENT_NONE;
}
static void handle_touch(int raw_x,int raw_y)
{
    if(s_ui.calibrating){
        if(s_ui.cal_step<0 || s_ui.cal_step>=CAL_COUNT) s_ui.cal_step=0;
        s_samples[s_ui.cal_step][0]=raw_x;
        s_samples[s_ui.cal_step][1]=raw_y;
        ++s_ui.cal_step;
        if(s_ui.cal_step==CAL_COUNT){
            if(calibrate_finish()){
                s_ui.calibrating=false;
                s_ui.calibrated=true;
                notice(save_cal()?"TOUCH CALIBRATED":"CAL OK / NVS NOT SAVED");
            }else{
                s_ui.cal_step=0;
                notice("BAD CAL - RETRY");
            }
        }
        return;
    }
    if(!s_ui.calibrated) return;
    int x,y;map_touch(raw_x,raw_y,&x,&y);
    if(y>=224){
        if(x<117) start_scan();
        else if(x<238) start_select();
        else if(x<356){
            if(s_stage!=ACTION_IDLE) notice("BUSY - WAIT");
            else {
                kcx_uart_command("AT+DISCON");
                notice("DISCONNECT REQUESTED");
            }
        }else{
            int pages=(int)((s_ui.bt.device_count+3u)/4u);
            s_ui.page=pages ? (s_ui.page+1)%pages : 0;
        }
    }else if(y>=81 && y<221){
        int index=s_ui.page*4+(y-81)/35;
        if(index>=0 && (size_t)index<s_ui.bt.device_count){
            s_ui.selected=index;
            notice("DEVICE SELECTED / CONNECT");
        }
    }else if(y<45 && x>=390){
        s_ui.calibrating=true;s_ui.cal_step=0;
        notice("CALIBRATION RESTART");
    }
}
void app_main(void)
{
    esp_err_t err=nvs_flash_init();
    if(err==ESP_ERR_NVS_NO_FREE_PAGES || err==ESP_ERR_NVS_NEW_VERSION_FOUND){
        ESP_ERROR_CHECK(nvs_flash_erase());
        err=nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    kcx_state_init(&s_ui.bt);
    s_ui.selected=-1;
    s_ui.calibrated=load_cal();
    s_ui.calibrating=!s_ui.calibrated;
    s_events=xQueueCreate(APP_EVENTS_CAPACITY,sizeof(event_t));
    if(!s_events){ESP_LOGE(TAG,"event queue failed");return;}
    if(!ui_display_init()){ESP_LOGE(TAG,"FSA506 initialization failed");return;}
    ui_display_draw(&s_ui);
    if(!touch_resistive_start(touch_callback)) notice("TOUCH INIT FAILED");
    if(!kcx_uart_init(kcx_callback)){notice("KCX UART INIT FAILED");}
    else {
        kcx_uart_command("AT+");
        vTaskDelay(pdMS_TO_TICKS(200));
        kcx_uart_command("AT+GMR?");
        vTaskDelay(pdMS_TO_TICKS(200));
        kcx_uart_command("AT+BT_MODE?");
        vTaskDelay(pdMS_TO_TICKS(200));
        kcx_uart_command("AT+STATUS?");
    }
    ui_display_draw(&s_ui);
    int64_t last_poll=esp_timer_get_time();
    for(;;){
        event_t ev;
        bool dirty=false;
        if(xQueueReceive(s_events,&ev,pdMS_TO_TICKS(100))==pdTRUE){
            /* Drain UART bursts before redrawing slow parallel LCD tiles.
               Without coalescing, SCAN floods the event queue. */
            do {
                if(ev.kind==EVENT_TOUCH_UP){handle_touch(ev.x,ev.y);dirty=true;}
                else if(ev.kind==EVENT_KCX_LINE) dirty=handle_kcx(ev.line) || dirty;
            } while(xQueueReceive(s_events,&ev,0)==pdTRUE);
        }
        int64_t now=esp_timer_get_time();
        if(s_stage!=ACTION_IDLE && now>s_deadline){
            s_stage=ACTION_IDLE;
            notice("KCX COMMAND TIMEOUT");
            dirty=true;
        }
        if(now-last_poll>3000000LL && s_stage==ACTION_IDLE){
            kcx_uart_command("AT+STATUS?");
            last_poll=now;
        }
        if(dirty) ui_display_draw(&s_ui);
    }
}
