#pragma once
#include <stdbool.h>
#include <stddef.h>
typedef void (*kcx_uart_line_callback_t)(const char *line);
bool kcx_uart_init(kcx_uart_line_callback_t on_line);
bool kcx_uart_command(const char *command);
