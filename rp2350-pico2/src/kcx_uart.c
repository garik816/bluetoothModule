/*
 * KCX BT EMITTER v1.7 (115200 UART AT).
 * UART0 GP0 TX->KCX RX and GP1 RX<-KCX TX: use 3.3V<>5V level shifter
 * whenever KCX module exposes 5V logic. Both sides common digital GND.
 */
#include "kcx_uart.h"
#include "pins.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"

static kcx_line_callback_t s_callback;
static char s_line[160];
static unsigned s_index;

void kcx_uart_init(kcx_line_callback_t callback)
{
    s_callback=callback;
    gpio_init(KCX_MODE);gpio_put(KCX_MODE,1);gpio_set_dir(KCX_MODE,GPIO_OUT);
    uart_init(uart0,115200);
    gpio_set_function(KCX_UART_TX,GPIO_FUNC_UART);
    gpio_set_function(KCX_UART_RX,GPIO_FUNC_UART);
    uart_set_format(uart0,8,1,UART_PARITY_NONE);
    uart_set_hw_flow(uart0,false,false);
    puts("KCX v1.7: UART0 115200, MODE=EMITTER");
}
void kcx_uart_poll(void)
{
    unsigned budget=256;
    while(budget-- && uart_is_readable(uart0)){
        uint8_t ch=(uint8_t)uart_getc(uart0);
        if(ch=='\r'||ch=='\n'){
            if(s_index){
                s_line[s_index]=0;
                printf("KCX RX: %s\n",s_line);
                if(s_callback)s_callback(s_line);
                s_index=0;
            }
        }else if(ch>=32 && ch<127){
            if(s_index+1<sizeof(s_line))s_line[s_index++]=(char)ch;
            else {s_index=0;puts("KCX RX overflow - line dropped");}
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
