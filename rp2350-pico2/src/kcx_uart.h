#pragma once
#include <stdbool.h>
typedef void (*kcx_line_callback_t)(const char *line);
void kcx_uart_init(kcx_line_callback_t callback);
void kcx_uart_poll(void);
bool kcx_uart_send(const char *command);
