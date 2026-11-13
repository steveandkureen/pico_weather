# Locate the Pico SDK. Order: -DPICO_SDK_PATH=..., then $PICO_SDK_PATH,
# then ~/src/pico-sdk. Must be included before project().
if (NOT PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
  set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()
if (NOT PICO_SDK_PATH AND EXISTS "$ENV{HOME}/src/pico-sdk/pico_sdk_init.cmake")
  set(PICO_SDK_PATH "$ENV{HOME}/src/pico-sdk")
endif()
if (NOT PICO_SDK_PATH)
  message(FATAL_ERROR "Pico SDK not found. Set PICO_SDK_PATH to its location.")
endif()

get_filename_component(PICO_SDK_PATH "${PICO_SDK_PATH}" REALPATH)
set(PICO_SDK_PATH "${PICO_SDK_PATH}" CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)
