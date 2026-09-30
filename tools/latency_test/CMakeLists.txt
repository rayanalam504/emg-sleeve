# Latency test firmware: sends the same sawtooth as CC 11 over BLE and CC 12
# over USB MIDI. Shares ble_midi.cpp, config.h and the GATT file with the main
# firmware in the repo root. USB MIDI only, so no serial console.
cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(PICO_BOARD pico2_w CACHE STRING "Board type")
include(${ROOT}/pico_sdk_import.cmake)
project(latency_test C CXX ASM)
pico_sdk_init()

add_executable(latency_test latency_main.cpp ${ROOT}/ble_midi.cpp usb_descriptors.c)
pico_enable_stdio_uart(latency_test 0)
pico_enable_stdio_usb(latency_test 0)
target_include_directories(latency_test PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${ROOT})
target_link_libraries(latency_test
        pico_stdlib
        tinyusb_device
        pico_cyw43_arch_none
        pico_btstack_ble
        pico_btstack_cyw43)
pico_btstack_make_gatt_header(latency_test PRIVATE "${ROOT}/midi_service.gatt")
pico_add_extra_outputs(latency_test)
