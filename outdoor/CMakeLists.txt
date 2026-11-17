# Outdoor station firmware (Raspberry Pi Pico 2, no WiFi).
#   cmake -S outdoor -B build/outdoor -G Ninja && cmake --build build/outdoor
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2 CACHE STRING "Board type")
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/pico_sdk_import.cmake)

project(wx_outdoor C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../common ${CMAKE_BINARY_DIR}/common)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../pico_drivers ${CMAKE_BINARY_DIR}/pico_drivers)

add_executable(wx_outdoor src/main.cpp)
target_include_directories(wx_outdoor PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_options(wx_outdoor PRIVATE -Wall -Wextra)
target_link_libraries(wx_outdoor PRIVATE
  wx_common
  wx_pico_drivers
  pico_stdlib
  pico_rand
  hardware_adc
  hardware_watchdog
)
pico_enable_stdio_usb(wx_outdoor 1)
pico_enable_stdio_uart(wx_outdoor 0)
pico_add_extra_outputs(wx_outdoor)

# Bench tool: USB serial <-> LoRa module UART passthrough (not station firmware).
add_executable(wx_radio_bridge src/radio_bridge.cpp)
target_include_directories(wx_radio_bridge PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_options(wx_radio_bridge PRIVATE -Wall -Wextra)
target_link_libraries(wx_radio_bridge PRIVATE pico_stdlib hardware_uart)
pico_enable_stdio_usb(wx_radio_bridge 1)
pico_enable_stdio_uart(wx_radio_bridge 0)
pico_add_extra_outputs(wx_radio_bridge)
