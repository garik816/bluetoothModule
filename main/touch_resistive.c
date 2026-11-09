/*
 * Adapted from Silvus Configurator hw/esp32s3-fsa506.
 * Ampire FSA506 board: 4-wire resistive touch without SPI controller.
 * FFC30=X1, FFC31=X2, FFC32=Y1, FFC33=Y2 is based on the related Ampire H3
 * datasheet; pad numbering is user-confirmed, electrode pairs need ohmmeter QA.
 *
 * GPIO outputs provide a voltage gradient along one axis while ADC1 samples
 * the other. For now, this is RAW-COORDINATE DIAGNOSTICS ONLY: no radio writes,
 * The application handles screen-coordinate calibration and UI actions.
 */

#include "touch_resistive.h"

#include <stdint.h>
#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "kcx_touch";

enum { AXIS_SAMPLES = 5, POLL_MS = 50, TOUCH_LOG_MS = 250 };

static const gpio_num_t s_x1 = (gpio_num_t)1;
static const gpio_num_t s_x2 = (gpio_num_t)2;
static const gpio_num_t s_y1 = (gpio_num_t)7;
static const gpio_num_t s_y2 = (gpio_num_t)8;
static adc_oneshot_unit_handle_t s_adc;
static adc_channel_t s_adc_x1;
static adc_channel_t s_adc_y1;
static touch_resistive_callback_t s_callback;

/* All electrodes high-impedance between scans; never drive two axes at once. */
static esp_err_t release_touch_pins(void)
{
    const uint64_t mask = (1ULL << s_x1) | (1ULL << s_x2) |
                          (1ULL << s_y1) | (1ULL << s_y2);
    const gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

/* Pen detection: short both X terminals to GND, pull Y1 up weakly.
 * Contact connects the Y film to the grounded X film; open film stays high.
 */
static bool is_pressed(void)
{
    if (release_touch_pins() != ESP_OK) return false;
    if (gpio_set_level(s_x1, 0) != ESP_OK ||
        gpio_set_level(s_x2, 0) != ESP_OK ||
        gpio_set_direction(s_x1, GPIO_MODE_OUTPUT) != ESP_OK ||
        gpio_set_direction(s_x2, GPIO_MODE_OUTPUT) != ESP_OK ||
        gpio_set_pull_mode(s_y1, GPIO_PULLUP_ONLY) != ESP_OK) {
        (void)release_touch_pins();
        return false;
    }
    esp_rom_delay_us(300);
    const bool down = gpio_get_level(s_y1) == 0;
    (void)release_touch_pins();
    return down;
}

/* Configure an ADC pin AFTER releasing the previous digital GPIO mode.
 * adc_oneshot_config_channel() also restores its analog IO mux on ESP-IDF 5.5.
 */
static esp_err_t read_axis(gpio_num_t high, gpio_num_t low,
                           adc_channel_t channel, int *result)
{
    esp_err_t err = release_touch_pins();
    if (err != ESP_OK) return err;

    const adc_oneshot_chan_cfg_t adc_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    err = adc_oneshot_config_channel(s_adc, channel, &adc_cfg);
    if (err != ESP_OK) goto done;

    err = gpio_set_level(high, 1);
    if (err != ESP_OK) goto done;
    err = gpio_set_level(low, 0);
    if (err != ESP_OK) goto done;
    err = gpio_set_direction(high, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) goto done;
    err = gpio_set_direction(low, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) goto done;
    esp_rom_delay_us(300);

    /* Discard the first sample after the electrode/ADC-mux transition. */
    int unused = 0;
    err = adc_oneshot_read(s_adc, channel, &unused);
    if (err != ESP_OK) goto done;
    int sum = 0;
    for (int i = 0; i < AXIS_SAMPLES; i++) {
        int raw = 0;
        err = adc_oneshot_read(s_adc, channel, &raw);
        if (err != ESP_OK) goto done;
        sum += raw;
    }
    *result = sum / AXIS_SAMPLES;

done:
    (void)release_touch_pins();
    return err;
}

static void touch_task(void *arg)
{
    (void)arg;
    bool active = false;
    unsigned down_count = 0;
    unsigned up_count = 0;
    int64_t last_log = 0;
    unsigned adc_errors = 0;
    int last_x = 0;
    int last_y = 0;

    ESP_LOGI(TAG, "4-wire touch diagnostic ready: FFC30/X1=GPIO%d FFC31/X2=GPIO%d FFC32/Y1=GPIO%d FFC33/Y2=GPIO%d",
             (int)s_x1, (int)s_x2, (int)s_y1, (int)s_y2);
    ESP_LOGI(TAG, "4-wire ADC1 touchscreen input ready");

    for (;;) {
        int x = 0, y = 0;
        bool down = is_pressed();
        if (down) {
            /* X coordinate: drive X1 high / X2 low; read Y1.
             * Y coordinate: drive Y1 high / Y2 low; read X1.
             */
            esp_err_t err = read_axis(s_x1, s_x2, s_adc_y1, &x);
            if (err == ESP_OK) err = read_axis(s_y1, s_y2, s_adc_x1, &y);
            if (err != ESP_OK) {
                down = false;
                if ((++adc_errors % 100U) == 1U)
                    ESP_LOGW(TAG, "Touch ADC error: %s", esp_err_to_name(err));
            } else {
                down = is_pressed(); // reject incomplete reads during release
                if (down) {
                    last_x = x;
                    last_y = y;
                }
            }
        }

        if (down) {
            up_count = 0;
            if (down_count < 2) ++down_count;
            if (down_count >= 2) {
                const int64_t now = esp_timer_get_time();
                if (!active) {
                    active = true;
                    ESP_LOGI(TAG, "TOUCH DOWN raw_x=%d raw_y=%d", x, y);
                    if (s_callback) s_callback(TOUCH_RESISTIVE_DOWN, x, y);
                    last_log = now;
                } else if (now - last_log >= TOUCH_LOG_MS * 1000LL) {
                    ESP_LOGI(TAG, "TOUCH MOVE raw_x=%d raw_y=%d", x, y);
                    if (s_callback) s_callback(TOUCH_RESISTIVE_MOVE, x, y);
                    last_log = now;
                }
            }
        } else {
            down_count = 0;
            if (up_count < 2) ++up_count;
            if (active && up_count >= 2) {
                ESP_LOGI(TAG, "TOUCH UP");
                if (s_callback) s_callback(TOUCH_RESISTIVE_UP, last_x, last_y);
                active = false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

bool touch_resistive_start(touch_resistive_callback_t callback)
{
    s_callback = callback;
    const gpio_num_t pins[] = {s_x1, s_x2, s_y1, s_y2};
    for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        const int p = (int)pins[i];
        if (p < 1 || p > 10 || p == 3 || (p >= 4 && p <= 6) || p == 9 || p == 10) {
            ESP_LOGE(TAG, "GPIO%d is reserved, boot-strapped or not a safe spare ADC1 pin", p);
            return false;
        }
        for (unsigned j = 0; j < i; ++j) {
            if (pins[j] == pins[i]) {
                ESP_LOGE(TAG, "Touch pins must be distinct (GPIO%d repeated)", p);
                return false;
            }
        }
    }

    adc_unit_t unit_x, unit_y;
    if (adc_oneshot_io_to_channel(s_x1, &unit_x, &s_adc_x1) != ESP_OK ||
        adc_oneshot_io_to_channel(s_y1, &unit_y, &s_adc_y1) != ESP_OK ||
        unit_x != ADC_UNIT_1 || unit_y != ADC_UNIT_1) {
        ESP_LOGE(TAG, "Touch X1/Y1 pins must support ADC1");
        return false;
    }
    const adc_oneshot_unit_init_cfg_t init = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&init, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC1 init failed: %s", esp_err_to_name(err));
        return false;
    }
    if (release_touch_pins() != ESP_OK ||
        xTaskCreate(touch_task, "resistive_touch", 4096, NULL, 2, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Touch task/pin initialization failed");
        (void)release_touch_pins();
        (void)adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
        return false;
    }
    return true;
}


