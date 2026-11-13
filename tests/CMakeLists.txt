# Host-side tests for the shared code. Runs on the PC, no Pico needed:
#   cmake -S tests -B build/tests && cmake --build build/tests && ctest --test-dir build/tests
cmake_minimum_required(VERSION 3.13)
project(wx_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../common ${CMAKE_BINARY_DIR}/common)

add_executable(wx_tests test_main.cpp)
target_link_libraries(wx_tests PRIVATE wx_common)
target_compile_options(wx_tests PRIVATE -Wall -Wextra -fsanitize=address,undefined)
target_link_options(wx_tests PRIVATE -fsanitize=address,undefined)

enable_testing()
add_test(NAME wx_tests COMMAND wx_tests)
