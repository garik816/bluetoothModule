#include "kcx_uart.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define KCX_UART UART_NUM_1
#define KCX_UART_BAUD 115200
#define KCX_UART_TX_GPIO 4
#define KCX_UART_RX_GPIO 5
#define KCX_MODE_GPIO 9
#define KCX_LINK_GPIO 10
static const char *TAG="kcx_uart";
static kcx_uart_line_callback_t s_cb;

static void receive_task(void *unused)
{
    (void)unused;
    uint8_t buf[96];
    char line[160];
    size_t used=0;
    for (;;) {
        int count=uart_read_bytes(KCX_UART, buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (count<0) continue;
        for (int i=0;i<count;++i) {
            uint8_t ch=buf[i];
            if (ch=='\r' || ch=='\n') {
                if (used) {
                    line[used]=0;
                    ESP_LOGI(TAG,"RX: %s",line);
                    if (s_cb) s_cb(line);
                    used=0;
                }
            } else if (ch>=32 && ch<127) {
                if (used+1 < sizeof(line)) line[used++]=(char)ch;
                else { used=0; ESP_LOGW(TAG,"UART line too long, discarded"); }
            }
        }
    }
}
bool kcx_uart_init(kcx_uart_line_callback_t cb)
{
    s_cb=cb;
    const uart_config_t config={
        .baud_rate=KCX_UART_BAUD, .data_bits=UART_DATA_8_BITS,
        .parity=UART_PARITY_DISABLE, .stop_bits=UART_STOP_BITS_1,
        .flow_ctrl=UART_HW_FLOWCTRL_DISABLE, .source_clk=UART_SCLK_DEFAULT
    };
    esp_err_t err=uart_driver_install(KCX_UART,2048,0,0,NULL,0);
    if(err==ESP_OK) err=uart_param_config(KCX_UART,&config);
    if(err==ESP_OK) err=uart_set_pin(KCX_UART,KCX_UART_TX_GPIO,KCX_UART_RX_GPIO,
                                    UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);
    if(err!=ESP_OK){ESP_LOGE(TAG,"UART init: %s",esp_err_to_name(err));return false;}
    /* KCX mode HIGH = transmitter, LOW = receiver, per KCX v1.7 examples.
       Keep MODE high even when GPIO state is transient during startup.
       External 10k pull-up recommended. */
    gpio_set_level((gpio_num_t)KCX_MODE_GPIO,1);
    gpio_set_direction((gpio_num_t)KCX_MODE_GPIO,GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)KCX_LINK_GPIO,GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)KCX_LINK_GPIO,GPIO_PULLDOWN_ONLY);
    if(xTaskCreate(receive_task,"kcx_uart_rx",4096,NULL,5,NULL)!=pdPASS) return false;
    ESP_LOGI(TAG,"KCX UART1 115200 TX GPIO4 -> KCX RX, RX GPIO5 <- KCX TX");
    return true;
}
bool kcx_uart_command(const char *cmd)
{
    if(!cmd || strncmp(cmd,"AT+",3)!=0 || strlen(cmd)>95) return false;
    char line[100];
    snprintf(line,sizeof(line),"%s\r\n",cmd);
    int sent=uart_write_bytes(KCX_UART,line,strlen(line));
    ESP_LOGI(TAG,"TX: %s",cmd);
    return sent==(int)strlen(line);
}
