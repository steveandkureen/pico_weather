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

execute_process(COMMAND git describe --always --dirty
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE WX_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if (NOT WX_VERSION)
  set(WX_VERSION "unknown")
endif()

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../common ${CMAKE_BINARY_DIR}/common)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../pico_drivers ${CMAKE_BINARY_DIR}/pico_drivers)

# Web pages → gzipped C++ arrays.
set(WEB_DIR ${CMAKE_CURRENT_LIST_DIR}/../web)
set(WEB_FILES
  ${WEB_DIR}/app.js
  ${WEB_DIR}/config.html
  ${WEB_DIR}/layout_a.html
  ${WEB_DIR}/layout_b.html
  ${WEB_DIR}/layout_c.html
  ${WEB_DIR}/layout_d.html
)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/web_assets.cpp
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_LIST_DIR}/../tools/embed_web.py
          ${CMAKE_BINARY_DIR}/web_assets.cpp ${WEB_FILES}
  DEPENDS ${WEB_FILES} ${CMAKE_CURRENT_LIST_DIR}/../tools/embed_web.py
  COMMENT "Embedding web pages"
)

add_executable(wx_base
  src/main.cpp
  src/app.cpp
  src/net/forecast_client.cpp
  src/net/http_server.cpp
  src/net/setup_servers.cpp
  src/net/wifi_manager.cpp
  ${CMAKE_BINARY_DIR}/web_assets.cpp
)
target_include_directories(wx_base PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_options(wx_base PRIVATE -Wall -Wextra)
target_compile_definitions(wx_base PRIVATE
  WX_DEV_WIFI_SSID="${WX_DEV_WIFI_SSID}"
  WX_DEV_WIFI_PASSWORD="${WX_DEV_WIFI_PASSWORD}"
  WX_VERSION="${WX_VERSION}"
  CYW43_HOST_NAME="weather"
)
target_link_libraries(wx_base PRIVATE
  wx_common
  wx_pico_drivers
  pico_stdlib
  hardware_watchdog
  # Poll mode: everything runs in the main loop, no background interrupts
  # touching lwIP. Simplest to reason about.
  pico_cyw43_arch_lwip_poll
  pico_lwip_mdns
  pico_lwip_sntp
)
pico_enable_stdio_usb(wx_base 1)
pico_enable_stdio_uart(wx_base 0)
pico_add_extra_outputs(wx_base)
