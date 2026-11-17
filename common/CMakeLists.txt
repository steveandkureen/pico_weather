# Code shared by both firmwares and the host tests. Plain C++, no Pico SDK.
add_library(wx_common STATIC
  ${CMAKE_CURRENT_LIST_DIR}/src/bme280.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/config_form.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/crc32.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/dxlr22.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/flash_store.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/forecast.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/health.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/http.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/json.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/packet.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/packet_framer.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/rain_ledger.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/settings.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/station.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/timezone.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/wind.cpp
)
target_include_directories(wx_common PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_compile_features(wx_common PUBLIC cxx_std_17)
target_compile_options(wx_common PRIVATE -Wall -Wextra)
