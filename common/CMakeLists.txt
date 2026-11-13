# Code shared by both firmwares and the host tests. Plain C++, no Pico SDK.
add_library(wx_common STATIC
  ${CMAKE_CURRENT_LIST_DIR}/src/packet.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/rain_ledger.cpp
)
target_include_directories(wx_common PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_compile_features(wx_common PUBLIC cxx_std_17)
target_compile_options(wx_common PRIVATE -Wall -Wextra -Wconversion)
