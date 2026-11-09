/*
 * FSA506 480x272 8080/RGB565 timings adapted from Silvus Configurator
 * hw/esp32s3-fsa506: fsa506_status.c (H525/V286 falling edge, GRAM Y544).
 * RP2350 PIO0 SM0, one DMA 32-bit word per LCD bus byte.
 */
#include "fsa506.h"
#include "font.h"
#include "pins.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "fsa506.pio.h"

#define FSA_BURST (FSA_WIDTH * FSA_TILE_HEIGHT * 2)
static PIO s_pio=pio0;
static uint s_sm=0;
static int s_dma=-1;
static int s_tile_top;
static uint32_t s_tile[FSA_BURST];
static uint s_program_offset;

static void bus_idle(void)
{
    while (!pio_sm_is_tx_fifo_empty(s_pio,s_sm)) tight_loop_contents();
    /* FIFO-empty may precede completion of WR strobe. */
    sleep_us(3);
}
static void bus_byte(uint8_t data)
{
    pio_sm_put_blocking(s_pio,s_sm,(uint32_t)data);
    bus_idle();
}
static void reg8(uint8_t address,uint8_t value)
{
    gpio_put(LCD_CS,0);
    gpio_put(LCD_RS,0);
    bus_byte(address);
    gpio_put(LCD_RS,1);
    bus_byte(value);
    gpio_put(LCD_CS,1);
}
static void reg16(uint8_t high,uint16_t value)
{
    reg8(high,(uint8_t)(value>>8));
    reg8((uint8_t)(high+1),(uint8_t)value);
}
static void window(int x0,int y0,int x1,int y1)
{
    reg16(0x00,(uint16_t)x0);
    reg16(0x02,(uint16_t)(x1-1));
    reg16(0x04,(uint16_t)y0);
    reg16(0x06,(uint16_t)(y1-1));
}
static void panel_init(void)
{
    static const struct{uint8_t reg,value;} init[]={
        {0x40,0x12},{0x41,5},{0x42,6},
        {0x0a,0},{0x0b,0},{0x0c,0},
        {0x10,0x0d},{0x11,5},{0x12,0},{0x13,0},
        {0x14,0},{0x15,41},{0x1c,0},{0x1d,0},
        {0x1e,0},{0x1f,10},{0x26,0},{0x27,0},{0x28,0},
        {0x2d,0x0c},{0x30,0},{0x31,0},{0x32,0},{0x33,0}
    };
    for(uint i=0;i<sizeof(init)/sizeof(init[0]);++i)reg8(init[i].reg,init[i].value);
    reg16(0x08,FSA_WIDTH);
    reg16(0x16,43);reg16(0x18,FSA_WIDTH);reg16(0x1a,525);
    reg16(0x20,12);reg16(0x22,FSA_HEIGHT);reg16(0x24,286);
    reg16(0x34,FSA_WIDTH);reg16(0x36,2*FSA_HEIGHT);
    window(0,0,FSA_WIDTH,FSA_HEIGHT);
    reg8(0x29,1);
    sleep_ms(60);
}
bool fsa_init(void)
{
    gpio_init(LCD_RD);gpio_put(LCD_RD,1);gpio_set_dir(LCD_RD,GPIO_OUT);
    gpio_init(LCD_CS);gpio_put(LCD_CS,1);gpio_set_dir(LCD_CS,GPIO_OUT);
    gpio_init(LCD_RS);gpio_put(LCD_RS,0);gpio_set_dir(LCD_RS,GPIO_OUT);
    s_program_offset=pio_add_program(s_pio,&fsa_i80_program);
    for(uint pin=LCD_D0;pin<LCD_D0+8;++pin)pio_gpio_init(s_pio,pin);
    pio_gpio_init(s_pio,LCD_WR);
    pio_sm_config cfg=fsa_i80_program_get_default_config(s_program_offset);
    sm_config_set_out_pins(&cfg,LCD_D0,8);
    sm_config_set_sideset_pins(&cfg,LCD_WR);
    sm_config_set_out_shift(&cfg,true,false,32);
    sm_config_set_fifo_join(&cfg,PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&cfg,8.0f);
    pio_sm_init(s_pio,s_sm,s_program_offset,&cfg);
    pio_sm_set_consecutive_pindirs(s_pio,s_sm,LCD_D0,8,true);
    pio_sm_set_consecutive_pindirs(s_pio,s_sm,LCD_WR,1,true);
    pio_sm_set_pins_with_mask(s_pio,s_sm,1u<<LCD_WR,1u<<LCD_WR);
    pio_sm_set_enabled(s_pio,s_sm,true);
    s_dma=dma_claim_unused_channel(true);
    if(s_dma<0)return false;
    panel_init();
    puts("FSA506: RP2350 PIO 8-bit 8080 active, 480x272");
    return true;
}
void fsa_begin_tile(int top)
{
    s_tile_top=top;
    memset(s_tile,0,sizeof(s_tile));
}
void fsa_rect(int x,int y,int w,int h,uint16_t color)
{
    int left=x<0?0:x;
    int right=x+w>FSA_WIDTH?FSA_WIDTH:x+w;
    int top=y<s_tile_top?s_tile_top:y;
    int bottom=y+h>s_tile_top+FSA_TILE_HEIGHT?s_tile_top+FSA_TILE_HEIGHT:y+h;
    if(bottom>FSA_HEIGHT)bottom=FSA_HEIGHT;
    if(right<=left||bottom<=top)return;
    for(int py=top;py<bottom;++py){
        size_t idx=((size_t)(py-s_tile_top)*FSA_WIDTH+(size_t)left)*2u;
        for(int px=left;px<right;++px){
            s_tile[idx++]=(uint8_t)(color>>8);
            s_tile[idx++]=(uint8_t)color;
        }
    }
}
void fsa_char(int x,int y,uint32_t cp,int scale,uint16_t color)
{
    const uint8_t *glyph=font8x12(cp);
    if(!glyph||scale<1||scale>3)return;
    for(int row=0;row<12;++row){
        int py=y+row*scale;
        if(py+scale<=s_tile_top || py>=s_tile_top+FSA_TILE_HEIGHT)continue;
        uint8_t bits=glyph[row];
        for(int col=0;col<8;++col)
            if(bits&(0x80u>>col))fsa_rect(x+col*scale,py,scale,scale,color);
    }
}
static uint32_t utf8_next(const char **it)
{
    const unsigned char *p=(const unsigned char *)*it;
    if(!*p)return 0;
    if(p[0]<0x80){(*it)++;return p[0];}
    if((p[0]&0xe0)==0xc0 && p[1] && (p[1]&0xc0)==0x80){
        uint32_t cp=((p[0]&0x1f)<<6)|(p[1]&0x3f);*it+=2;return cp;
    }
    if((p[0]&0xf0)==0xe0 && p[1] && p[2] &&
       (p[1]&0xc0)==0x80 && (p[2]&0xc0)==0x80){
        uint32_t cp=((p[0]&0x0f)<<12)|((p[1]&0x3f)<<6)|(p[2]&0x3f);*it+=3;return cp;
    }
    (*it)++;return '?';
}
void fsa_text(int x,int y,const char *utf8,int scale,uint16_t color)
{
    if(!utf8||scale<1)return;
    while(*utf8 && x+8*scale<=FSA_WIDTH){
        uint32_t cp=utf8_next(&utf8);
        if(!cp)break;
        fsa_char(x,y,cp,scale,color);
        x+=8*scale;
    }
}
bool fsa_present_tile(void)
{
    int y=s_tile_top;
    if(y<0||y>=FSA_HEIGHT)return false;
    int bottom=(y+FSA_TILE_HEIGHT>FSA_HEIGHT)?FSA_HEIGHT:y+FSA_TILE_HEIGHT;
    window(0,y,FSA_WIDTH,bottom);
    gpio_put(LCD_CS,0);
    gpio_put(LCD_RS,0);
    bus_byte(0xc1);
    gpio_put(LCD_RS,1);
    dma_channel_config cfg=dma_channel_get_default_config(s_dma);
    channel_config_set_transfer_data_size(&cfg,DMA_SIZE_32);
    channel_config_set_read_increment(&cfg,true);
    channel_config_set_write_increment(&cfg,false);
    channel_config_set_dreq(&cfg,pio_get_dreq(s_pio,s_sm,true));
    dma_channel_configure(s_dma,&cfg,&s_pio->txf[s_sm],s_tile,
                          (uint32_t)((bottom-y)*FSA_WIDTH*2),true);
    dma_channel_wait_for_finish_blocking(s_dma);
    bus_idle();
    gpio_put(LCD_RS,0);
    bus_byte(0x80);
    gpio_put(LCD_CS,1);
    return true;
}
