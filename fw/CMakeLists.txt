cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD waveshare_core2350b CACHE STRING "Cordyceps RP2350B module")
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(cordyceps C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(cordyceps main.c)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(cordyceps 
            pico_stdlib 
            pico_usb_reset 
            pico_unique_id)

target_compile_definitions(cordyceps PRIVATE
    PICO_ENABLE_USB_RESET_VIA_VENDOR_INTERFACE=1
    PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=0
    PICO_USB_RESET_INCLUDE_DEFAULT_APP_DRIVER_CB=1
    PICO_USB_RESET_SUPPORT_MS_OS_20_DESCRIPTOR=1
    PICO_USB_RESET_MS_OS_20_DESCRIPTOR_ITF=0
)

pico_add_extra_outputs(cordyceps)
