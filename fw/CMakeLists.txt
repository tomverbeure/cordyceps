cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD waveshare_core2350b CACHE STRING "Cordyceps RP2350B module")
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(cordyceps C CXX ASM)
set(CMAKE_C_STANDARD 11)

#============================================================
# Compile a USB-enabled picotool
#============================================================
include(ExternalProject)

set(CORDYCEPS_PICOTOOL_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps")
set(CORDYCEPS_PICOTOOL_EXECUTABLE "${CORDYCEPS_PICOTOOL_INSTALL_DIR}/picotool/picotool")

ExternalProject_Add(cordyceps_picotool
    PREFIX "${CMAKE_BINARY_DIR}/_deps/picotool-usb"
    GIT_REPOSITORY https://github.com/raspberrypi/picotool.git
    GIT_TAG 2.3.1
    GIT_SHALLOW TRUE
    UPDATE_DISCONNECTED TRUE
    CMAKE_ARGS
        "-DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}"
        "-DCMAKE_BUILD_TYPE:STRING=Release"
        "-DPICO_SDK_PATH:PATH=${PICO_SDK_PATH}"
        "-DPICOTOOL_NO_LIBUSB:BOOL=OFF"
        "-DPICOTOOL_FLAT_INSTALL:BOOL=ON"
        "-DCMAKE_INSTALL_PREFIX:PATH=${CORDYCEPS_PICOTOOL_INSTALL_DIR}"
    BUILD_ALWAYS TRUE
    BUILD_BYPRODUCTS "${CORDYCEPS_PICOTOOL_EXECUTABLE}"
    # picotool can silently build without libusb; require a USB command to exist.
    TEST_COMMAND "${CORDYCEPS_PICOTOOL_EXECUTABLE}" help load
    TEST_AFTER_INSTALL TRUE
)

add_executable(picotool IMPORTED GLOBAL)
set_target_properties(picotool PROPERTIES IMPORTED_LOCATION "${CORDYCEPS_PICOTOOL_EXECUTABLE}")
add_dependencies(picotool cordyceps_picotool)
#============================================================

# Register our picotool before SDK initialization can look for an installed one.
pico_sdk_init()

add_executable(cordyceps
            main.c
            tusb_descriptors.c
            bdm.c
            rtc.c
            scpi_uart.c
            vxw_uart.c
            ring_buf.c
            nvsram_emu.c
            gdb.c
        )

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(cordyceps 
            pico_stdlib 
            tinyusb_device
            pico_usb_reset 
            pico_unique_id
            pico_aon_timer
            hardware_i2c
            )

target_compile_definitions(cordyceps PRIVATE
    # Add dedicated USB interface to reset the device in BOOTSEL mode
    PICO_ENABLE_USB_RESET_VIA_VENDOR_INTERFACE=1
    # Don't reset the device in BOOTSEL mode by selecting 1200 BAUD on the serial interface
    PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=0

    # Use a TinyUSB callback for the reset interface
    PICO_USB_RESET_INCLUDE_DEFAULT_APP_DRIVER_CB=1

    # Tells Windows to use the WinUSB driver, which allows picotools to use the
    # USB reset interface without a custom Windows driver
    PICO_USB_RESET_SUPPORT_MS_OS_20_DESCRIPTOR=1

    # Three CDC ports occupy interfaces 0-5. The reset interface is 6.
    PICO_USB_RESET_MS_OS_20_DESCRIPTOR_ITF=6
)

pico_add_extra_outputs(cordyceps)

# Can't use stdio for USB when you have more than 1 USB CDC-ACM interface.
pico_enable_stdio_usb(cordyceps 0)

# stdio printf etc should never go to a UART.
pico_enable_stdio_uart(cordyceps 0)

