idf_component_register(
    SRCS "main.c" "kcx_protocol.c" "kcx_uart.c" "ui_display.c" "touch_resistive.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_uart esp_lcd esp_adc nvs_flash esp_timer
)
