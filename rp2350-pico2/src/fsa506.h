#pragma once
#include <stdbool.h>
#include <stdint.h>
#define FSA_WIDTH 480
#define FSA_HEIGHT 272
#define FSA_TILE_HEIGHT 8
#define FSA_RGB565(r,g,b) (uint16_t)((((r)&0xf8u)<<8)|(((g)&0xfcu)<<3)|(((b)&0xf8u)>>3))
bool fsa_init(void);
void fsa_begin_tile(int top);
void fsa_rect(int x,int y,int w,int h,uint16_t color);
void fsa_char(int x,int y,uint32_t cp,int scale,uint16_t color);
void fsa_text(int x,int y,const char *utf8,int scale,uint16_t color);
bool fsa_present_tile(void);
