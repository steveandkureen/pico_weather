# Base station firmware (Raspberry Pi Pico 2 W).
#   cmake -S base -B build/base -G Ninja && cmake --build build/base
#
# Dev builds can bake in a WiFi network so the board joins it on its own,
# skipping the saved settings and setup mode:
#   cmake -S base -B build/base -DWX_DEV_WIFI_SSID=MyNet -DWX_DEV_WIFI_PASSWORD=secret
# or export WX_DEV_WIFI_SSID / WX_DEV_WIFI_PASSWORD before configuring.
# Leave them empty for a release build.
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Board type")
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/pico_sdk_import.cmake)

project(wx_base C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

set(WX_DEV_WIFI_SSID "$ENV{WX_DEV_WIFI_SSID}" CACHE STRING "Dev only: WiFi network to join, overriding saved settings")
set(WX_DEV_WIFI_PASSWORD "$ENV{WX_DEV_WIFI_PASSWORD}" CACHE STRING "Dev only: password for WX_DEV_WIFI_SSID")
if (WX_DEV_WIFI_SSID)
  message(WARNING "DEV BUILD: WiFi forced to '${WX_DEV_WIFI_SSID}'. Do not install this build.")
endif()

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../common ${CMAKE_BINARY_DIR}/common)

add_executable(wx_base src/main.cpp src/wifi_supervisor.cpp)
target_include_directories(wx_base PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_options(wx_base PRIVATE -Wall -Wextra)
target_compile_definitions(wx_base PRIVATE
  WX_DEV_WIFI_SSID="${WX_DEV_WIFI_SSID}"
  WX_DEV_WIFI_PASSWORD="${WX_DEV_WIFI_PASSWORD}"
)
target_link_libraries(wx_base PRIVATE
  wx_common
  pico_stdlib
  hardware_watchdog
  # Poll mode: everything runs in the main loop, no background interrupts
  # touching lwIP. Simplest to reason about.
  pico_cyw43_arch_lwip_poll
)
pico_enable_stdio_usb(wx_base 1)
pico_enable_stdio_uart(wx_base 0)
pico_add_extra_outputs(wx_base)
