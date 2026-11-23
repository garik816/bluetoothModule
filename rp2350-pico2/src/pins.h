#pragma once
/* Raspberry Pi Pico 2 RP2350A. LCD wiring UNCHANGED. No Touch or MODE GPIO. */
#define LCD_D0        2   /* DB0..DB7 use GP2..GP9 contiguous PIO bus */
#define LCD_WR       10   /* active-low pulse, PIO side-set */
#define LCD_RS       11   /* RS: 0 command, 1 data */
#define LCD_CS       12
#define LCD_RD       13   /* held HIGH: write only */
#define KCX_UART_TX   0   /* Pico TX => KCX RX, voltage translation required */
#define KCX_UART_RX   1   /* Pico RX <= KCX TX, voltage translation required */
