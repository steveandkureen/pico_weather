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

add_executable(wx_outdoor src/main.cpp)
target_compile_options(wx_outdoor PRIVATE -Wall -Wextra)
target_link_libraries(wx_outdoor PRIVATE
  wx_common
  pico_stdlib
  pico_rand
  hardware_watchdog
)
pico_enable_stdio_usb(wx_outdoor 1)
pico_enable_stdio_uart(wx_outdoor 0)
pico_add_extra_outputs(wx_outdoor)
