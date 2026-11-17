# Pico SDK drivers shared by both firmwares (I2C, flash, LoRa UART port).
add_library(wx_pico_drivers STATIC
  ${CMAKE_CURRENT_LIST_DIR}/src/pico_radio_port.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pico_flash.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pico_i2c_bus.cpp
)
target_include_directories(wx_pico_drivers PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_compile_options(wx_pico_drivers PRIVATE -Wall -Wextra)
target_link_libraries(wx_pico_drivers PUBLIC
  wx_common
  pico_stdlib
  pico_flash
  hardware_flash
  hardware_i2c
  hardware_uart
  hardware_irq
)
