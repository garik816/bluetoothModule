/*
 * FSA506 480x272 i80 driver, adapted from Silvus Configurator
 * hw/esp32s3-fsa506 (original fsa506_status.c).
 * Reuses verified timings / DMA tiled draw / 5x7 glyph primitives.
 */
#include "ui_display.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "driver/gpio.h"
#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_io.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#define LCD_W 480
#define LCD_H 272
#define TILE_H 16
#define LCD_I80_HZ (2 * 1000 * 1000)
#define FSA_PLL_R41 5
#define FSA_PLL_R42 6
#define FSA_HSYNC_WIDTH 41
#define FSA_VSYNC_WIDTH 10
#define FSA_H_START 43
#define FSA_V_START 12
#define FSA_H_TOTAL 525
#define FSA_V_TOTAL 286
#define FSA_GRAM_Y (2 * LCD_H)
#define FSA_OUTPUT_CONTROL 0x0c
#define RGB565(r,g,b) ((uint16_t)((((uint16_t)(r) & 0xF8u) << 8) | (((uint16_t)(g) & 0xFCu) << 3) | (((uint16_t)(b) & 0xF8u) >> 3)))
#define COLOR_NAVY RGB565(16,32,58)
#define COLOR_WHITE RGB565(255,255,255)
#define COLOR_BLACK RGB565(24,37,52)
#define COLOR_GRAY RGB565(213,226,237)
#define COLOR_BLUE RGB565(13,113,207)
#define COLOR_RED RGB565(187,48,54)
#define COLOR_GREEN RGB565(32,155,103)
static const char *TAG = "kcx_fsa506";
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_tx_done;
static int s_tile_y;
static DMA_ATTR uint16_t s_tile[LCD_W * TILE_H];

static esp_err_t fsa_reg(uint8_t address, uint8_t value)
{
    // 8-bit 8080: command address (RS=0) then one data byte (RS=1).
    return esp_lcd_panel_io_tx_param(s_io, address, &value, 1);
}

static esp_err_t fsa_word(uint8_t register_high, unsigned value)
{
    esp_err_t err = fsa_reg(register_high, (uint8_t)(value >> 8));
    if (err == ESP_OK) err = fsa_reg(register_high + 1, (uint8_t)value);
    return err;
}

static esp_err_t fsa_window(int x1, int y1, int x2, int y2)
{
    esp_err_t err = fsa_word(0x00, (unsigned)x1);
    if (err == ESP_OK) err = fsa_word(0x02, (unsigned)(x2 - 1));
    if (err == ESP_OK) err = fsa_word(0x04, (unsigned)y1);
    if (err == ESP_OK) err = fsa_word(0x06, (unsigned)(y2 - 1));
    return err;
}

static esp_err_t fsa_initialise(void)
{
    /*
     * Experimental timings derived from FSA506 320x240 reference and generic
     * WQVGA 480x272 porch values; NOT verified for AM480272C3TMQW-TW0H.
     * Do not enable the bus until the host pinout, backlight drive, and
     * 8-bit 8080 mode/65K RGB565 straps have been confirmed.
     */
ESP_LOGI(TAG, "FSA506 Ampire reference timing H525/V286");
    static const struct { uint8_t reg; uint8_t value; } init[] = {
        {0x40,0x12}, {0x41,FSA_PLL_R41}, {0x42,FSA_PLL_R42},
        {0x0a,0x00}, {0x0b,0x00}, {0x0c,0x00},
        {0x10,0x0d}, {0x11,0x05},
        {0x12,0x00}, {0x13,0x00},
        {0x14,0x00}, {0x15,FSA_HSYNC_WIDTH},
        {0x1c,0x00}, {0x1d,0x00},
        {0x1e,0x00}, {0x1f,FSA_VSYNC_WIDTH},
        {0x26,0x00}, {0x27,0x00}, {0x28,0x00},
        {0x2d,FSA_OUTPUT_CONTROL},
        {0x30,0x00}, {0x31,0x00},
        {0x32,0x00}, {0x33,0x00},
    };
    for (size_t i = 0; i < sizeof(init) / sizeof(init[0]); ++i) {
        ESP_RETURN_ON_ERROR(fsa_reg(init[i].reg, init[i].value), TAG, "FSA506 register");
    }

    ESP_RETURN_ON_ERROR(fsa_word(0x08, LCD_W), TAG, "input X resolution");
    ESP_RETURN_ON_ERROR(fsa_word(0x16, FSA_H_START), TAG, "H start");
    ESP_RETURN_ON_ERROR(fsa_word(0x18, LCD_W), TAG, "H active");
    ESP_RETURN_ON_ERROR(fsa_word(0x1a, FSA_H_TOTAL), TAG, "H total");
    ESP_RETURN_ON_ERROR(fsa_word(0x20, FSA_V_START), TAG, "V start");
    ESP_RETURN_ON_ERROR(fsa_word(0x22, LCD_H), TAG, "V active");
    ESP_RETURN_ON_ERROR(fsa_word(0x24, FSA_V_TOTAL), TAG, "V total");
    ESP_RETURN_ON_ERROR(fsa_word(0x34, LCD_W), TAG, "GRAM X");
    ESP_RETURN_ON_ERROR(fsa_word(0x36, FSA_GRAM_Y), TAG, "GRAM Y");
    ESP_RETURN_ON_ERROR(fsa_window(0, 0, LCD_W, LCD_H), TAG, "GRAM window");
    ESP_RETURN_ON_ERROR(fsa_reg(0x29, 0x01), TAG, "latch output timing");
    vTaskDelay(pdMS_TO_TICKS(60));
    return ESP_OK;
}
static const char s_alphabet[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.:-/?_%";
static const uint8_t s_glyph[][5] = {
    {0,0,0,0,0},
    {0x3e,0x51,0x49,0x45,0x3e}, {0,0x42,0x7f,0x40,0},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x45,0x4b,0x31,0},
    {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e},
    {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
    {0,0x41,0x7f,0x41,0}, {0x20,0x40,0x41,0x3f,1},
    {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
    {0x7f,2,0x0c,2,0x7f}, {0x7f,4,8,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,9,9,9,6},
    {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,9,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {1,1,0x7f,1,1},
    {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,8,0x14,0x63},
    {7,8,0x70,8,7}, {0x61,0x51,0x49,0x45,0x43},
    {0,0x60,0x60,0,0}, {0,0x36,0x36,0,0},
    {8,8,8,8,8}, {0x20,0x10,8,4,2},
    {2,1,0x51,9,6}, {0x40,0x40,0x40,0x40,0x40},
    {0x63,0x13,8,0x64,0x63}
};

static inline uint16_t wire_rgb565(uint16_t color)
{
    // With IM4=1 (default), send RGB565 high byte then low byte.
    // With IM4=0, send low byte then high byte. ESP32 is little-endian.
return (uint16_t)((color << 8) | (color >> 8));
}

static inline void pixel(int x, int y, uint16_t color)
{
    if ((unsigned)x >= LCD_W || y < s_tile_y || y >= s_tile_y + TILE_H || y >= LCD_H)
        return;
    s_tile[(y - s_tile_y) * LCD_W + x] = wire_rgb565(color);
}

static void rectangle(int x, int y, int w, int h, uint16_t color)
{
    int left = x < 0 ? 0 : x;
    int right = x + w > LCD_W ? LCD_W : x + w;
    int top = y < s_tile_y ? s_tile_y : y;
    int bottom = y + h > s_tile_y + TILE_H ? s_tile_y + TILE_H : y + h;
    if (bottom > LCD_H) bottom = LCD_H;
    if (right <= left || bottom <= top) return;
    const uint16_t v = wire_rgb565(color);
    for (int py = top; py < bottom; ++py) {
        uint16_t *row = &s_tile[(py - s_tile_y) * LCD_W];
        for (int px = left; px < right; ++px) row[px] = v;
    }
}

static void print_text(int x, int y, const char *str, int scale, uint16_t color)
{
    if (!str || scale < 1) return;
    const int advance = 6 * scale;
    const char *question = strchr(s_alphabet, '?');
    const unsigned fallback = question ? (unsigned)(question - s_alphabet) : 0;
    for (int ch = 0; str[ch] && x + ch * advance < LCD_W - 3; ++ch) {
        const char *p = strchr(s_alphabet, toupper((unsigned char)str[ch]));
        const unsigned idx = p ? (unsigned)(p - s_alphabet) : fallback;
        if (idx >= sizeof(s_glyph) / sizeof(s_glyph[0])) continue;
        for (int col = 0; col < 5; ++col) {
            const uint8_t bits = s_glyph[idx][col];
            for (int bit = 0; bit < 7; ++bit)
                if ((bits >> bit) & 1u)
                    rectangle(x + ch * advance + col * scale,
                              y + bit * scale, scale, scale, color);
        }
    }
}
static bool on_transfer_done(esp_lcd_panel_io_handle_t io,
                             esp_lcd_panel_io_event_data_t *event_data,
                             void *user_ctx)
{
    (void)io;
    (void)event_data;
    SemaphoreHandle_t sem = (SemaphoreHandle_t)user_ctx;
    BaseType_t woke = pdFALSE;
    xSemaphoreGiveFromISR(sem, &woke);
    return woke == pdTRUE;
}

/*
 * Bench pattern: eight color columns, position markers and text.
 * This runs once and persists while Silvus continues running.
 * It does not write to any radio registers.
 */
static bool submit_tile(int y, int bottom)
{
    xSemaphoreTake(s_tx_done, 0);
    esp_err_t err = fsa_window(0, y, LCD_W, bottom);
    if (err == ESP_OK) {
        err = esp_lcd_panel_io_tx_color(
            s_io, 0xc1, s_tile,
            (size_t)(bottom - y) * LCD_W * sizeof(s_tile[0]));
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FSA506 transfer [%d,%d) failed: %s",
                 y, bottom, esp_err_to_name(err));
        return false;
    }
    if (xSemaphoreTake(s_tx_done, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "FSA506 I80 DMA timeout at y=%d", y);
        return false;
    }
    if (esp_lcd_panel_io_tx_param(s_io, 0x80, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "FSA506 RAM write disable failed at y=%d", y);
        return false;
    }
    return true;
}

static void draw_button(int x, int y, int w, int h, const char *label, uint16_t color)
{
    rectangle(x, y, w, h, color);
    print_text(x+10, y+13, label, 2, COLOR_WHITE);
}
static void draw_screen(const ui_view_t *view)
{
    rectangle(0, 0, LCD_W, LCD_H, COLOR_WHITE);
    rectangle(0, 0, LCD_W, 43, COLOR_NAVY);
    print_text(14, 9, "BT AUDIO / KCX V1.7", 3, COLOR_WHITE);
    print_text(408, 5, "CAL", 1, COLOR_WHITE);
    if (view->calibrating) {
        static const int marks_x[5] = {45, 435, 435, 45, 240};
        static const int marks_y[5] = {70, 70, 210, 210, 136};
        print_text(24, 48, "TOUCH CALIBRATION", 2, COLOR_BLUE);
        if (view->cal_step >= 0 && view->cal_step < 5) {
            int cx=marks_x[view->cal_step], cy=marks_y[view->cal_step];
            rectangle(cx-12,cy-1,25,3,COLOR_RED);
            rectangle(cx-1,cy-12,3,25,COLOR_RED);
        }
        print_text(120, 242, "TOUCH THE CROSS", 2, COLOR_BLACK);
        return;
    }
    print_text(16, 51, "KCX:", 2, COLOR_BLACK);
    const char *status = !view->bt.ready ? "WAITING" :
        (view->bt.mode_known && !view->bt.transmitter) ? "RX MODE" :
        view->bt.connected ? "CONNECTED" :
        view->bt.scanning ? "SCANNING" : "READY";
    print_text(80, 51, status, 2, view->bt.connected ? COLOR_GREEN : COLOR_BLUE);
    if (view->bt.version[0]) print_text(250, 51, view->bt.version, 1, COLOR_BLACK);
    rectangle(9, 76, 460, 1, COLOR_GRAY);
    for (int i=0; i<4; ++i) {
        int index=view->page*4+i;
        int y=81+i*35;
        if ((size_t)index >= view->bt.device_count) break;
        kcx_device_t const *d=&view->bt.devices[index];
        if (index==view->selected) rectangle(10,y,456,33,COLOR_BLUE);
        char label[64];
        snprintf(label,sizeof(label),"%02d %.24s",index+1,d->name);
        print_text(17,y+4,label,2,index==view->selected ? COLOR_WHITE : COLOR_BLACK);
        print_text(290,y+12,d->mac,1,index==view->selected ? COLOR_WHITE : COLOR_BLUE);
    }
    draw_button(10,224,105,43,"SCAN",COLOR_BLUE);
    draw_button(124,224,108,43,"CONNECT",COLOR_GREEN);
    draw_button(241,224,110,43,"DROP",COLOR_BLUE);
    draw_button(360,224,108,43,"MORE",COLOR_NAVY);
    if (view->notice[0]) {
        rectangle(18,204,444,18,COLOR_GRAY);
        print_text(23,208,view->notice,1,COLOR_BLACK);
    }
}
bool ui_display_draw(const ui_view_t *view)
{
    if (!s_io || !view) return false;
    for (int y=0; y<LCD_H; y+=TILE_H) {
        s_tile_y=y;
        draw_screen(view);
        if (!submit_tile(y,y+TILE_H)) return false;
    }
    return true;
}
bool ui_display_init(void)
{
    s_tx_done=xSemaphoreCreateBinary();
    if (!s_tx_done) return false;
    gpio_num_t rd=(gpio_num_t)42;
    esp_err_t err=gpio_reset_pin(rd);
    if (err==ESP_OK) err=gpio_set_level(rd,1);
    if (err==ESP_OK) err=gpio_set_direction(rd,GPIO_MODE_OUTPUT);
    if (err!=ESP_OK) return false;
    const esp_lcd_i80_bus_config_t bus={
        .clk_src=LCD_CLK_SRC_DEFAULT,
        .dc_gpio_num=40, .wr_gpio_num=41,
        .data_gpio_nums={14,15,16,17,18,21,38,39},
        .bus_width=8, .max_transfer_bytes=sizeof(s_tile), .dma_burst_size=16
    };
    esp_lcd_i80_bus_handle_t bus_handle=NULL;
    err=esp_lcd_new_i80_bus(&bus,&bus_handle);
    if (err!=ESP_OK) { ESP_LOGE(TAG,"i80 bus: %s",esp_err_to_name(err)); return false; }
    const esp_lcd_panel_io_i80_config_t io={
        .cs_gpio_num=47,
        .pclk_hz=LCD_I80_HZ, .trans_queue_depth=2,
        .dc_levels={.dc_idle_level=0,.dc_cmd_level=0,.dc_dummy_level=0,.dc_data_level=1},
        .lcd_cmd_bits=8,.lcd_param_bits=8,
        .flags.swap_color_bytes=0,
        .on_color_trans_done=on_transfer_done,.user_ctx=s_tx_done
    };
    err=esp_lcd_new_panel_io_i80(bus_handle,&io,&s_io);
    if (err!=ESP_OK) { ESP_LOGE(TAG,"panel IO: %s",esp_err_to_name(err)); return false; }
    vTaskDelay(pdMS_TO_TICKS(120));
    err=fsa_initialise();
    if (err!=ESP_OK) { ESP_LOGE(TAG,"controller init: %s",esp_err_to_name(err));return false; }
    ESP_LOGI(TAG,"FSA506 480x272 active, touch GPIO 1 2 7 8");
    return true;
}
