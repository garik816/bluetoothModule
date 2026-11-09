/*
 * Adaptation of Silvus 4-wire resistive touchscreen algorithm to Pico SDK.
 * X1=GP26/ADC0, Y1=GP27/ADC1; X2=GP20, Y2=GP21.
 * Records 5-point calibration in reserved LAST 4096-byte flash sector.
 * Configure app / linker to leave that sector unused (see CMake file).
 */
#include "touch.h"
#include "pins.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/regs/addressmap.h"

#define CAL_MAGIC 0x46534154u
#define CAL_SECTOR_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define CAL_SAMPLES 5
static const uint pins[4]={TOUCH_X1,TOUCH_X2,TOUCH_Y1,TOUCH_Y2};

static uint32_t cal_crc(const touch_calibration_t *c)
{
    const uint8_t *p=(const uint8_t*)c;
    uint32_t crc=0xffffffffu;
    for(size_t i=0;i<offsetof(touch_calibration_t,checksum);++i){
        crc^=p[i];
        for(int b=0;b<8;++b)crc=(crc>>1)^((crc&1u)?0xedb88320u:0);
    }
    return ~crc;
}
static bool cal_valid(const touch_calibration_t *c)
{
    if(c->magic!=CAL_MAGIC || c->checksum!=cal_crc(c))return false;
    if(c->left<0||c->left>4095||c->right<0||c->right>4095||
       c->top<0||c->top>4095||c->bottom<0||c->bottom>4095)return false;
    return abs(c->right-c->left)>=700&&abs(c->bottom-c->top)>=700;
}
bool touch_calibration_load(touch_calibration_t *out)
{
    if(!out)return false;
    const touch_calibration_t *stored=(const touch_calibration_t *)
        (XIP_BASE+CAL_SECTOR_OFFSET);
    memcpy(out,stored,sizeof(*out));
    return cal_valid(out);
}
bool touch_calibration_save(const touch_calibration_t *cal)
{
    if(!cal||!cal_valid(cal))return false;
    static uint8_t page[FLASH_PAGE_SIZE] __attribute__((aligned(4)));
    memset(page,0xff,sizeof(page));
    memcpy(page,cal,sizeof(*cal));
    /* Single-core firmware: interrupt masking keeps USB IRQ from executing
       flash code during erase/program. Do not start another core. */
    uint32_t irq=save_and_disable_interrupts();
    flash_range_erase(CAL_SECTOR_OFFSET,FLASH_SECTOR_SIZE);
    flash_range_program(CAL_SECTOR_OFFSET,page,FLASH_PAGE_SIZE);
    restore_interrupts(irq);
    touch_calibration_t verify;
    return touch_calibration_load(&verify)&&
           memcmp(&verify,cal,sizeof(*cal))==0;
}
static void release_pins(void)
{
    for(size_t i=0;i<4;++i){
        gpio_init(pins[i]);
        gpio_set_dir(pins[i],GPIO_IN);
        gpio_disable_pulls(pins[i]);
    }
}
static void drive(uint pin,bool high)
{
    gpio_init(pin);
    gpio_put(pin,high?1:0);
    gpio_set_dir(pin,GPIO_OUT);
}
static bool is_pressed(void)
{
    release_pins();
    drive(TOUCH_X1,false);drive(TOUCH_X2,false);
    gpio_pull_up(TOUCH_Y1);
    busy_wait_us_32(250);
    bool pressed=!gpio_get(TOUCH_Y1);
    release_pins();
    return pressed;
}
static int read_axis(uint hi,uint lo,uint sample_gpio)
{
    release_pins();
    adc_gpio_init(sample_gpio);
    drive(hi,true);
    drive(lo,false);
    busy_wait_us_32(250);
    adc_select_input(sample_gpio==TOUCH_X1?0:1);
    (void)adc_read();
    int sum=0;
    for(int i=0;i<CAL_SAMPLES;++i)sum+=adc_read();
    release_pins();
    return sum/CAL_SAMPLES;
}
void touch_init(void)
{
    adc_init();
    release_pins();
}
bool touch_poll(touch_sample_t *sample)
{
    if(!sample)return false;
    static bool active;
    static uint32_t last_sample_ms;
    static int last_x,last_y;
    uint32_t now=to_ms_since_boot(get_absolute_time());
    if((uint32_t)(now-last_sample_ms)<30)return false;
    last_sample_ms=now;
    bool down=is_pressed();
    if(down){
        int x=read_axis(TOUCH_X1,TOUCH_X2,TOUCH_Y1);
        int y=read_axis(TOUCH_Y1,TOUCH_Y2,TOUCH_X1);
        down=is_pressed();
        if(down){last_x=x;last_y=y;active=true;}
    }
    if(!down && active){
        active=false;
        *sample=(touch_sample_t){.raw_x=last_x,.raw_y=last_y,.pressed=false};
        return true;
    }
    return false;
}
static int clamp(int v,int a,int b){return v<a?a:(v>b?b:v);}
static int map_axis(int v,int v0,int v1,int p0,int p1,int max)
{
    int d=v1-v0;if(abs(d)<100)return 0;
    return clamp(p0+(int)((int64_t)(v-v0)*(p1-p0)/d),0,max);
}
void touch_calibration_map(const touch_calibration_t *cal,int raw_x,int raw_y,int *x,int *y)
{
    if(!cal||!x||!y)return;
    *x=map_axis(raw_x,cal->left,cal->right,45,435,479);
    *y=map_axis(raw_y,cal->top,cal->bottom,70,210,271);
}
bool touch_calibration_make(const touch_sample_t samples[5],touch_calibration_t *out)
{
    if(!samples||!out)return false;
    *out=(touch_calibration_t){
        .magic=CAL_MAGIC,
        .left=(samples[0].raw_x+samples[3].raw_x)/2,
        .right=(samples[1].raw_x+samples[2].raw_x)/2,
        .top=(samples[0].raw_y+samples[1].raw_y)/2,
        .bottom=(samples[2].raw_y+samples[3].raw_y)/2,
    };
    out->checksum=cal_crc(out);
    if(!cal_valid(out))return false;
    int x,y;
    touch_calibration_map(out,samples[4].raw_x,samples[4].raw_y,&x,&y);
    return abs(x-240)<=65 && abs(y-136)<=50;
}
