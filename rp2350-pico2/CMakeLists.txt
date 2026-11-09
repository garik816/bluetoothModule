cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico2 CACHE STRING "Raspberry Pi Pico 2")
endif()
if(NOT DEFINED PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
endif()
if(NOT DEFINED PICO_SDK_PATH)
    message(FATAL_ERROR "Set PICO_SDK_PATH to your Pico SDK 2.3.1 directory")
endif()
include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
project(bluetooth_module_pico2 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()
add_executable(bluetooth_module_pico2
    src/main.c
    src/fsa506.c
    src/font.c
    src/ui.c
    src/touch.c
    src/kcx_uart.c
    src/kcx_protocol.c
)
target_include_directories(bluetooth_module_pico2 PRIVATE src)
pico_generate_pio_header(bluetooth_module_pico2 src/fsa506.pio)
target_link_libraries(bluetooth_module_pico2 PRIVATE
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_uart
    hardware_irq
    hardware_adc
    hardware_flash
    hardware_sync
    hardware_clocks
)
target_compile_options(bluetooth_module_pico2 PRIVATE -Wall -Wextra)
pico_enable_stdio_usb(bluetooth_module_pico2 1)
pico_enable_stdio_uart(bluetooth_module_pico2 0)
pico_add_extra_outputs(bluetooth_module_pico2)
