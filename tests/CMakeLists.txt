# Host-side tests for the shared code. Runs on the PC, no Pico needed:
#   cmake -S tests -B build/tests && cmake --build build/tests && ./build/tests/wx_tests
cmake_minimum_required(VERSION 3.13)
project(wx_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined)
add_link_options(-fsanitize=address,undefined)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../common ${CMAKE_BINARY_DIR}/common)

add_executable(wx_tests
  test_main.cpp
  test_core.cpp
  test_storage.cpp
  test_time.cpp
  test_sensors.cpp
  test_radio.cpp
  test_net.cpp
  test_station.cpp
)
target_link_libraries(wx_tests PRIVATE wx_common)
target_compile_options(wx_tests PRIVATE -Wall -Wextra)
target_compile_definitions(wx_tests PRIVATE WX_FIXTURE_DIR="${CMAKE_CURRENT_LIST_DIR}/fixtures")

enable_testing()
add_test(NAME wx_tests COMMAND wx_tests)
