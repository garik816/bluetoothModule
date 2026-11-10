/*
 * RP2350 Pico 2 / 4-wire resistive Touch diagnostic driver.
 * FSA506 connector 30 TOUCH1=GP26/ADC0 (X1), 31 TOUCH3=GP20 (X2),
 * 32 TOUCH2=GP27/ADC1 (Y1), 33 TOUCH4=GP21 (Y2).
 *
 * Two consecutive pressed polls and three released polls debounce a gesture;
 * one release -> one calibration point. Raw X/Y values are averaged.
 */
#include "touch.h"
#include "pins.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/regs/addressmap.h"

#define CAL_MAGIC 0x46534154u
#define CAL_SECTOR_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define AXIS_SAMPLES 6
#define POLL_INTERVAL_MS 25u
#define PRESS_DEBOUNCE 2u
#define RELEASE_DEBOUNCE 3u
#define RAW_REPORT_MS 200u

static const uint s_pins[4]={TOUCH_X1,TOUCH_X2,TOUCH_Y1,TOUCH_Y2};
static bool s_verbose=true,s_active=false;
static uint32_t s_last_poll_ms,s_last_raw_log,s_gesture_start;
static uint32_t s_sum_x,s_sum_y,s_points,s_gesture_id;
static uint32_t s_last_idle;
static uint8_t s_press_count,s_release_count;

static uint32_t now_ms(void){return to_ms_since_boot(get_absolute_time());}
void touch_debug_enable(bool enable){s_verbose=enable;printf("[TOUCH] raw diagnostics %s\n",enable?"ENABLED":"DISABLED");}
bool touch_debug_enabled(void){return s_verbose;}

static uint32_t cal_crc(const touch_calibration_t *c)
{
    const uint8_t *p=(const uint8_t*)c;
    uint32_t crc=0xffffffffu;
    for(size_t i=0;i<offsetof(touch_calibration_t,checksum);++i){
        crc^=p[i];
        for(int j=0;j<8;++j)crc=(crc>>1)^((crc&1u)?0xedb88320u:0);
    }
    return ~crc;
}
static bool cal_valid(const touch_calibration_t *c, bool verbose)
{
    if(c->magic!=CAL_MAGIC){
        if(verbose)printf("[CAL STORAGE] no saved calibration: magic=%08lx\n",(unsigned long)c->magic);
        return false;
    }
    uint32_t expected=cal_crc(c);
    if(c->checksum!=expected){
        if(verbose)printf("[CAL STORAGE] checksum mismatch: stored=%08lx computed=%08lx\n",
                          (unsigned long)c->checksum,(unsigned long)expected);
        return false;
    }
    if(c->left<0||c->left>4095||c->right<0||c->right>4095||
       c->top<0||c->top>4095||c->bottom<0||c->bottom>4095){
        if(verbose)printf("[CAL STORAGE] invalid raw endpoints X=%ld..%ld Y=%ld..%ld\n",
                 (long)c->left,(long)c->right,(long)c->top,(long)c->bottom);
        return false;
    }
    int dx=abs(c->right-c->left),dy=abs(c->bottom-c->top);
    if(dx<700||dy<700){
        if(verbose)printf("[CAL STORAGE] insufficient span X=%d Y=%d (min 700)\n",dx,dy);
        return false;
    }
    return true;
}
bool touch_calibration_load(touch_calibration_t *out)
{
    if(!out)return false;
    const touch_calibration_t *saved=(const touch_calibration_t*)(XIP_BASE+CAL_SECTOR_OFFSET);
    memcpy(out,saved,sizeof(*out));
    bool valid=cal_valid(out,true);
    if(valid)printf("[CAL STORAGE] restored X=%ld..%ld Y=%ld..%ld\n",
                    (long)out->left,(long)out->right,(long)out->top,(long)out->bottom);
    return valid;
}
bool touch_calibration_save(const touch_calibration_t *c)
{
    if(!c || !cal_valid(c,true))return false;
    static uint8_t page[FLASH_PAGE_SIZE] __attribute__((aligned(4)));
    memset(page,0xff,sizeof(page));
    memcpy(page,c,sizeof(*c));
    printf("[CAL STORAGE] erase/program flash sector at 0x%08lx, size=%lu\n",
           (unsigned long)CAL_SECTOR_OFFSET,(unsigned long)FLASH_SECTOR_SIZE);
    /* This firmware deliberately uses core 0 only. While programming flash,
       interrupt masking prevents the USB ISR from fetching XIP code. */
    uint32_t irq_state=save_and_disable_interrupts();
    flash_range_erase(CAL_SECTOR_OFFSET,FLASH_SECTOR_SIZE);
    flash_range_program(CAL_SECTOR_OFFSET,page,FLASH_PAGE_SIZE);
    restore_interrupts(irq_state);
    touch_calibration_t check;
    bool ok=touch_calibration_load(&check)&&memcmp(&check,c,sizeof(*c))==0;
    printf("[CAL STORAGE] verify: %s\n",ok?"PASS":"FAIL");
    return ok;
}
static void release_all(void)
{
    for(size_t i=0;i<4;++i){
        gpio_init(s_pins[i]);
        gpio_set_dir(s_pins[i],GPIO_IN);
        gpio_disable_pulls(s_pins[i]);
    }
}
static void drive(uint pin,bool high)
{
    gpio_init(pin);
    gpio_put(pin,high);
    gpio_set_dir(pin,GPIO_OUT);
}
static bool sense_pressed(uint32_t *digital_y1)
{
    /* Hold BOTH X electrodes at GND, use Y1 pull-up to detect contact. */
    release_all();
    drive(TOUCH_X1,false);drive(TOUCH_X2,false);
    gpio_pull_up(TOUCH_Y1);
    busy_wait_us_32(350);
    bool level=gpio_get(TOUCH_Y1);
    if(digital_y1)*digital_y1=(uint32_t)level;
    release_all();
    return !level;
}
static int read_axis(uint high_pin,uint low_pin,uint adc_pin)
{
    release_all();
    adc_gpio_init(adc_pin);
    drive(high_pin,true);
    drive(low_pin,false);
    busy_wait_us_32(300);
    adc_select_input(adc_pin==TOUCH_X1?0:1);
    (void)adc_read(); /* discard ADC sample after mux change */
    uint32_t sum=0;
    for(int i=0;i<AXIS_SAMPLES;++i)sum+=adc_read();
    release_all();
    return (int)(sum/AXIS_SAMPLES);
}
void touch_init(void)
{
    adc_init();
    release_all();
    printf("[TOUCH] RP2350 4-wire: X1=GP%d ADC0 X2=GP%d Y1=GP%d ADC1 Y2=GP%d\n",
           TOUCH_X1,TOUCH_X2,TOUCH_Y1,TOUCH_Y2);
    printf("[TOUCH] polling=%ums debounce press=%u release=%u samples=%u\n",
           POLL_INTERVAL_MS,PRESS_DEBOUNCE,RELEASE_DEBOUNCE,AXIS_SAMPLES);
}
/* Detect resistive film pairs WITHOUT a press.
 * Drive one terminal low; weak internal pull-up on the other. A matching
 * electrode pair pulls the input low even when the screen is untouched.
 * Use both directions to reduce false detections from stray leakage. */
static bool electrode_pair_connected(uint a,uint b)
{
    unsigned low_ab=0,low_ba=0;
    for(int direction=0;direction<2;++direction){
        uint drive_pin=direction?b:a;
        uint input_pin=direction?a:b;
        release_all();
        drive(drive_pin,false);
        gpio_pull_up(input_pin);
        busy_wait_us_32(600);
        for(int i=0;i<6;++i){
            if(!gpio_get(input_pin)){
                if(direction)++low_ba;
                else ++low_ab;
            }
            busy_wait_us_32(100);
        }
    }
    release_all();
    return low_ab>=5 && low_ba>=5;
}
static void electrode_pair_scan(void)
{
    const uint pins[4]={TOUCH_X1,TOUCH_X2,TOUCH_Y1,TOUCH_Y2};
    const int connector[4]={30,31,32,33};
    const char *const labels[4]={"TOUCH1","TOUCH3","TOUCH2","TOUCH4"};
    bool pair[4][4]={{false}};
    unsigned count=0;
    puts("[PAIR PROBE] screen RELEASED: detecting conductive electrode pairs via weak internal pull-up");
    for(int i=0;i<4;++i){
        for(int j=i+1;j<4;++j){
            pair[i][j]=electrode_pair_connected(pins[i],pins[j]);
            if(pair[i][j])++count;
            printf("[PAIR PROBE] LCD#%d %s GP%d <-> LCD#%d %s GP%d : %s\n",
                   connector[i],labels[i],pins[i],connector[j],labels[j],pins[j],
                   pair[i][j]?"CONDUCTIVE":"OPEN");
        }
    }
    if(count==2 && pair[0][1] && pair[2][3]){
        puts("[PAIR RESULT] expected X=LCD#30-#31 and Y=LCD#32-#33: electrical pairing OK.");
        puts("[PAIR RESULT] if ADC0 Y stays near 4095, verify analog contact, ADC GP26 and supply.");
    }else if(count==2 && pair[0][2] && pair[1][3]){
        puts("[PAIR RESULT] MISMATCH: films are LCD#30-#32 (TOUCH1/2), LCD#31-#33 (TOUCH3/4).");
        puts("[PAIR FIX] Swap display leads LCD#31 and LCD#32 at Pico end:");
        puts("[PAIR FIX] LCD#31 TOUCH3 -> GP27; LCD#32 TOUCH2 -> GP20.");
        puts("[PAIR FIX] Keep LCD#30 -> GP26 and LCD#33 -> GP21. Do not change firmware GPIO map.");
    }else if(count==2 && pair[0][3] && pair[1][2]){
        puts("[PAIR RESULT] alternate film pairs LCD#30-#33 and LCD#31-#32.");
        puts("[PAIR RESULT] check ohmmeter; pin reassignment would be required.");
    }else{
        printf("[PAIR RESULT] AMBIGUOUS: %u conductive pairs (expected exactly 2).\n",count);
        puts("[PAIR RESULT] release all fingers, verify no short/bridge, run 'p' again.");
        puts("[PAIR RESULT] alternatively, ohmmeter: each film is a resistance between two pins at rest.");
    }
}
void touch_debug_probe(void)
{
    uint32_t digital=1;
    bool pressed=sense_pressed(&digital);
    printf("[TOUCH PROBE] Y1_pullup=%lu pressed=%d\n",
           (unsigned long)digital,pressed);
    printf("[TOUCH PROBE] LCD#30 TOUCH1=GP%d (ADC0), #31 TOUCH3=GP%d, "
           "#32 TOUCH2=GP%d (ADC1), #33 TOUCH4=GP%d\n",
           TOUCH_X1,TOUCH_X2,TOUCH_Y1,TOUCH_Y2);
    if(pressed){
        int x=read_axis(TOUCH_X1,TOUCH_X2,TOUCH_Y1);
        int y=read_axis(TOUCH_Y1,TOUCH_Y2,TOUCH_X1);
        int y_reverse=read_axis(TOUCH_Y2,TOUCH_Y1,TOUCH_X1);
        printf("[TOUCH PROBE] PRESSED: ADC1/GP%d X=%d, ADC0/GP%d Y=%d, Y-reverse-drive=%d\n",
               TOUCH_Y1,x,TOUCH_X1,y,y_reverse);
        puts("[TOUCH PROBE] release the screen and press 'p' again for film pair test.");
    }else{
        electrode_pair_scan();
    }
}
bool touch_poll(touch_sample_t *sample)
{
    if(!sample)return false;
    uint32_t now=now_ms();
    if((uint32_t)(now-s_last_poll_ms)<POLL_INTERVAL_MS)return false;
    s_last_poll_ms=now;
    uint32_t y1_level;
    bool down=sense_pressed(&y1_level);
    if(down){
        s_release_count=0;
        if(s_press_count<PRESS_DEBOUNCE)s_press_count++;
        if(!s_active && s_press_count>=PRESS_DEBOUNCE){
            s_active=true;
            s_sum_x=s_sum_y=s_points=0;
            s_gesture_start=now;
            s_last_raw_log=0;
            ++s_gesture_id;
            printf("[TOUCH DOWN] gesture=%lu at %lums Y1_pullup=%lu\n",
                   (unsigned long)s_gesture_id,(unsigned long)now,(unsigned long)y1_level);
        }
        if(s_active){
            int raw_x=read_axis(TOUCH_X1,TOUCH_X2,TOUCH_Y1);
            int raw_y=read_axis(TOUCH_Y1,TOUCH_Y2,TOUCH_X1);
            s_sum_x+=(uint32_t)raw_x;
            s_sum_y+=(uint32_t)raw_y;
            ++s_points;
            if(s_verbose && (!s_last_raw_log || (uint32_t)(now-s_last_raw_log)>=RAW_REPORT_MS)){
                printf("[TOUCH RAW] gesture=%lu ADC_X=%d ADC_Y=%d n=%lu hold=%lums\n",
                       (unsigned long)s_gesture_id,raw_x,raw_y,
                       (unsigned long)s_points,(unsigned long)(now-s_gesture_start));
                s_last_raw_log=now;
            }
        }
    }else{
        s_press_count=0;
        if(s_active){
            if(s_release_count<RELEASE_DEBOUNCE)s_release_count++;
            if(s_release_count>=RELEASE_DEBOUNCE){
                s_active=false;
                s_release_count=0;
                if(!s_points){
                    puts("[TOUCH WARN] gesture had no valid ADC samples");
                    return false;
                }
                *sample=(touch_sample_t){
                    .raw_x=(int)(s_sum_x/s_points),
                    .raw_y=(int)(s_sum_y/s_points),
                    .pressed=false
                };
                printf("[TOUCH UP] gesture=%lu rawAverage=(%d,%d) samples=%lu duration=%lums\n",
                       (unsigned long)s_gesture_id,sample->raw_x,sample->raw_y,
                       (unsigned long)s_points,(unsigned long)(now-s_gesture_start));
                return true;
            }
        }else if(s_verbose && (uint32_t)(now-s_last_idle)>=3000u){
            printf("[TOUCH IDLE] no press, Y1_pullup=%lu uptime=%lums (touch and hold a calibration cross)\n",
                   (unsigned long)y1_level,(unsigned long)now);
            s_last_idle=now;
        }
    }
    return false;
}
static int clamp_i(int v,int lo,int hi){return v<lo?lo:(v>hi?hi:v);}
static int map_axis(int v,int raw0,int raw1,int screen0,int screen1,int max)
{
    int span=raw1-raw0;
    if(abs(span)<100)return 0;
    return clamp_i(screen0+(int)((int64_t)(v-raw0)*(screen1-screen0)/span),0,max);
}
void touch_calibration_map(const touch_calibration_t *c,int raw_x,int raw_y,int *x,int *y)
{
    if(!c||!x||!y)return;
    *x=map_axis(raw_x,c->left,c->right,45,435,479);
    *y=map_axis(raw_y,c->top,c->bottom,70,210,271);
}
bool touch_calibration_make(const touch_sample_t s[5],touch_calibration_t *out)
{
    if(!s||!out)return false;
    for(int i=0;i<5;++i)
        printf("[CAL] sample[%d] raw X=%d Y=%d\n",i+1,s[i].raw_x,s[i].raw_y);
    *out=(touch_calibration_t){
        .magic=CAL_MAGIC,
        .left=(s[0].raw_x+s[3].raw_x)/2,
        .right=(s[1].raw_x+s[2].raw_x)/2,
        .top=(s[0].raw_y+s[1].raw_y)/2,
        .bottom=(s[2].raw_y+s[3].raw_y)/2,
    };
    out->checksum=cal_crc(out);
    int dx=out->right-out->left,dy=out->bottom-out->top;
    printf("[CAL] endpoint X=%ld..%ld (signed span %d), Y=%ld..%ld (signed span %d)\n",
           (long)out->left,(long)out->right,dx,
           (long)out->top,(long)out->bottom,dy);
    if(!cal_valid(out,true)){
        puts("[CAL ERROR] raw axis outside 0..4095 or |span|<700.");
        if(abs(dy)<100 && out->top>3950 && out->bottom>3950){
            puts("[CAL HINT] GP26/ADC0 is SATURATED near 4095 at all Y positions.");
            puts("[CAL HINT] possible wrong electrical electrode pairing (#30/#32 vs #30/#31).");
            puts("[CAL HINT] release finger, send 'p' in USB console to measure all 6 pairs.");
        }else{
            puts("[CAL HINT] inspect electrode pairing, raw samples and ADC wiring.");
        }
        return false;
    }
    int x,y;
    touch_calibration_map(out,s[4].raw_x,s[4].raw_y,&x,&y);
    printf("[CAL] center measured screen=(%d,%d), expected=(240,136), delta=(%d,%d), tolerance=(65,50)\n",
           x,y,abs(x-240),abs(y-136));
    if(abs(x-240)>65||abs(y-136)>50){
        puts("[CAL ERROR] center verification failed; check sensor axis/order or cross placement.");
        return false;
    }
    puts("[CAL] all five points validated");
    return true;
}
