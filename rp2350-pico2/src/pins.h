#pragma once
/* Raspberry Pi Pico 2 RP2350A. GP* names mean GPIO, not physical pin number. */
#define LCD_D0        2   /* DB0..DB7 use GP2..GP9 contiguous PIO bus */
#define LCD_WR       10   /* active-low pulse, PIO side-set */
#define LCD_RS       11   /* RS: 0 command, 1 data */
#define LCD_CS       12
#define LCD_RD       13   /* held HIGH: write only */
#define KCX_UART_TX   0   /* Pico TX => KCX RX, voltage translation required */
#define KCX_UART_RX   1   /* Pico RX <= KCX TX, voltage translation required */
#define KCX_MODE     15   /* HIGH=emitter; 5V-module side level shifter */
#define TOUCH_X1     26   /* ADC0 */
#define TOUCH_X2     20
#define TOUCH_Y1     27   /* ADC1 */
#define TOUCH_Y2     21
