/*
 * KCX_BT_EMITTER v1.7: UART0 at 115200.
 * RX IRQ ring buffers burst scan results during slow PIO/DMA LCD redraws.
 * GP0 TX -> KCX RX, GP1 RX <- KCX TX; KCX mode selected by PCB jumper.
 */
#include "kcx_uart.h"
#include "pins.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"

#define RX_RING_SIZE 4096u
#define RX_RING_MASK (RX_RING_SIZE - 1)
static volatile uint16_t rx_w,rx_r;
static uint8_t s_ring[RX_RING_SIZE];
static kcx_line_callback_t s_callback;
static char s_line[160];
static unsigned s_index;
static volatile uint32_t s_overflow,s_rx_bytes;
static uint32_t s_rx_lines;

static void on_uart_rx(void)
{
    while(uart_is_readable(uart0)){
        uint8_t b=(uint8_t)uart_getc(uart0);
        ++s_rx_bytes;
        uint16_t next=(uint16_t)((rx_w+1)&RX_RING_MASK);
        if(next==rx_r)++s_overflow;
        else {s_ring[rx_w]=b;rx_w=next;}
    }
}
void kcx_uart_init(kcx_line_callback_t callback)
{
    s_callback=callback;
    /* No GP15 or software MODE signal: KCX v1.7 uses a hardware TX/RX strap. */
    uart_init(uart0,115200);
    gpio_set_function(KCX_UART_TX,GPIO_FUNC_UART);
    gpio_set_function(KCX_UART_RX,GPIO_FUNC_UART);
    uart_set_format(uart0,8,1,UART_PARITY_NONE);
    uart_set_hw_flow(uart0,false,false);
    irq_set_exclusive_handler(UART0_IRQ,on_uart_rx);
    irq_set_enabled(UART0_IRQ,true);
    uart_set_irq_enables(uart0,true,false);
    puts("[KCX] UART0 115200 IRQ RX active; GP15 unused, mode set on KCX PCB");
}
void kcx_uart_poll(void)
{
    static uint32_t overflow_previous;
    if(s_overflow!=overflow_previous){
        printf("KCX UART RX overflow: %lu bytes lost\n",(unsigned long)s_overflow);
        overflow_previous=s_overflow;
    }
    unsigned budget=1024;
    while(budget-- && rx_r!=rx_w){
        uint8_t ch=s_ring[rx_r];
        rx_r=(uint16_t)((rx_r+1)&RX_RING_MASK);
        if(ch=='\r'||ch=='\n'){
            if(s_index){
                s_line[s_index]=0;
                ++s_rx_lines;
                printf("KCX RX: %s\n",s_line);
                if(s_callback)s_callback(s_line);
                s_index=0;
            }
        }else if(ch>=32 && ch!=127){
            /* Keep bytes >=128 intact for UTF-8 device names. */
            if(s_index+1<sizeof(s_line))s_line[s_index++]=(char)ch;
            else {s_index=0;puts("KCX RX oversized line dropped");}
        }
    }
}
bool kcx_uart_send(const char *command)
{
    if(!command || strncmp(command,"AT+",3)!=0 || strlen(command)>95)return false;
    printf("KCX TX: %s\n",command);
    uart_puts(uart0,command);
    uart_puts(uart0,"\r\n");
    return true;
}

uint32_t kcx_uart_rx_count(void){return s_rx_bytes;}
uint32_t kcx_uart_line_count(void){return s_rx_lines;}
uint32_t kcx_uart_overflow_count(void){return s_overflow;}
