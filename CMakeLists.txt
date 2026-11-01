cmake_minimum_required(VERSION 3.20)
option(CEIL_HOST_TESTS "Build portable algorithm tests only" OFF)
if(CEIL_HOST_TESTS)
  project(ceilometer_tests C)
  enable_testing()
  add_executable(burst_tests tests/burst_tests.c src/stream.c src/integration.c)
  target_include_directories(burst_tests PRIVATE tests/fakes include)
  target_compile_options(burst_tests PRIVATE -Wall -Wextra -Werror)
  add_test(NAME burst_tests COMMAND burst_tests)
  add_executable(integration_tests tests/integration_tests.c src/integration.c)
  target_include_directories(integration_tests PRIVATE include)
  target_compile_options(integration_tests PRIVATE -Wall -Wextra -Werror)
  add_test(NAME integration_tests COMMAND integration_tests)
  add_executable(core_tests tests/core_tests.c src/sequence.c src/correlation.c src/protocol.c src/lt8722_codec.c)
  target_include_directories(core_tests PRIVATE include)
  target_compile_options(core_tests PRIVATE -Wall -Wextra -Werror)
  add_test(NAME core_tests COMMAND core_tests)
  add_executable(thermal_tests tests/thermal_tests.c src/temperature_control.c src/pid_controller.c src/parameters.c)
  target_include_directories(thermal_tests PRIVATE include)
  target_compile_options(thermal_tests PRIVATE -Wall -Wextra -Werror)
  add_test(NAME thermal_tests COMMAND thermal_tests)
  add_executable(sensor_tests tests/sensor_tests.c src/sensors.c src/temperature_control.c src/pid_controller.c src/parameters.c)
  target_include_directories(sensor_tests PRIVATE tests/fakes include)
  target_compile_options(sensor_tests PRIVATE -Wall -Wextra -Werror)
  add_test(NAME sensor_tests COMMAND sensor_tests)
  add_executable(interlock_tests tests/interlock_tests.c src/temperature_control.c src/pid_controller.c
    src/sequence.c src/correlation.c src/protocol.c src/parameters.c src/parameter_journal.c
    src/telemetry.c src/sipm_bias.c src/integration.c tests/fakes/stream.c tests/fakes/storage_backend.c)
  target_include_directories(interlock_tests PRIVATE tests/fakes include)
  target_compile_definitions(interlock_tests PRIVATE CEIL_ENABLE_TEC=1 CEIL_ENABLE_LASER=1
    CEIL_ENABLE_BIAS=1 CEIL_BIAS_MAX_CODE=60000)
  target_compile_options(interlock_tests PRIVATE -Wall -Wextra -Werror)
  add_test(NAME interlock_tests COMMAND interlock_tests)
  add_executable(parameter_tests tests/parameter_tests.c src/parameters.c src/parameter_journal.c)
  target_include_directories(parameter_tests PRIVATE include)
  target_compile_options(parameter_tests PRIVATE -Wall -Wextra -Werror)
  add_test(NAME parameter_tests COMMAND parameter_tests)
  add_executable(telemetry_tests tests/telemetry_tests.c src/telemetry.c src/board.c src/lt8722.c
    src/lt8722_codec.c src/parameters.c src/temperature_control.c src/pid_controller.c src/sipm_bias.c)
  target_include_directories(telemetry_tests PRIVATE tests/fakes include)
  target_compile_definitions(telemetry_tests PRIVATE CEIL_ENABLE_TEC=1 CEIL_ENABLE_BIAS=1 CEIL_BIAS_MAX_CODE=60000)
  target_compile_options(telemetry_tests PRIVATE -Wall -Wextra -Werror)
  add_test(NAME telemetry_tests COMMAND telemetry_tests)
  find_program(CEIL_NODE_EXECUTABLE node)
  if(CEIL_NODE_EXECUTABLE)
    execute_process(COMMAND "${CEIL_NODE_EXECUTABLE}" --version
      OUTPUT_VARIABLE CEIL_NODE_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX REPLACE "^v" "" CEIL_NODE_VERSION "${CEIL_NODE_VERSION}")
    if(CEIL_NODE_VERSION VERSION_GREATER_EQUAL "22.0.0")
      add_test(NAME gui_protocol_tests COMMAND "${CEIL_NODE_EXECUTABLE}"
        --test "${CMAKE_CURRENT_LIST_DIR}/gui/tests/protocol.test.mjs")
    endif()
  endif()
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME release_tests COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/tests/test_release.py")
    add_test(NAME dashboard_tests COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/tests/test_dashboard.py")
    add_test(NAME stream_client_tests COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/tests/test_stream_client.py")
    add_test(NAME gui_bundle_freshness COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/gui/build_bundle.py" --check)
  endif()
  return()
endif()
set(PICO_BOARD pico2 CACHE STRING "Pico SDK board")
if(NOT PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
  set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()
if(NOT PICO_SDK_PATH)
  message(FATAL_ERROR "Set PICO_SDK_PATH to a Pico SDK >= 2.3 installation")
endif()
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(ceilometer C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()
option(CEIL_OVERCLOCK_BENCH "500MHz/1.5V RAM diagnostic, power drivers disabled" OFF)
option(CEIL_ENABLE_LASER "Allow commissioned laser output" OFF)
option(CEIL_ENABLE_BIAS "Allow commissioned SiPM bias output" OFF)
option(CEIL_ENABLE_TEC "Allow commissioned TEC output" OFF)
unset(CEIL_LASER_MAX_CODE CACHE) # Replaced by persistent laser_max_code.
set(CEIL_BIAS_MAX_CODE 0 CACHE STRING "Bench calibrated bias DAC ceiling (0 disables)")
foreach(limit CEIL_BIAS_MAX_CODE)
  if(NOT "${${limit}}" MATCHES "^[0-9]+$" OR ${limit} GREATER 65535)
    message(FATAL_ERROR "${limit} must be an integer in 0..65535")
  endif()
endforeach()
add_executable(ceilometer src/main.c src/sequence.c src/prn_tx.c src/sipm_counter.c
  src/bin_acquisition.c src/stream.c src/integration.c src/correlation.c src/protocol.c src/board.c
  src/lt8722.c src/lt8722_codec.c src/sensors.c src/test_pulse.c
  src/pid_controller.c src/temperature_control.c src/parameters.c
  src/parameter_journal.c src/parameter_storage.c src/telemetry.c src/sipm_bias.c)
if(CEIL_OVERCLOCK_BENCH)
  if(CEIL_ENABLE_LASER OR CEIL_ENABLE_BIAS OR CEIL_ENABLE_TEC)
    message(FATAL_ERROR "Overclock diagnostics require all power build flags OFF")
  endif()
  pico_set_binary_type(ceilometer copy_to_ram)
  target_compile_definitions(ceilometer PRIVATE CEIL_OVERCLOCK_BENCH=1)
endif()
# Execute application from SRAM as in the qualified overclock diagnostic.
pico_set_binary_type(ceilometer copy_to_ram)
# Keep QSPI at 50MHz even at clk_sys=500MHz, including after flash Save.
target_compile_definitions(bs2_default PRIVATE PICO_FLASH_SPI_CLKDIV=10)
target_include_directories(ceilometer PRIVATE include)
target_compile_options(ceilometer PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(ceilometer PRIVATE
  CEIL_ENABLE_LASER=$<BOOL:${CEIL_ENABLE_LASER}>
  CEIL_ENABLE_BIAS=$<BOOL:${CEIL_ENABLE_BIAS}>
  CEIL_ENABLE_TEC=$<BOOL:${CEIL_ENABLE_TEC}>
  CEIL_BIAS_MAX_CODE=${CEIL_BIAS_MAX_CODE}
  "USBD_MANUFACTURER=\"Fogcast\""
  "USBD_PRODUCT=\"Fogcast Ceilometer\""
  PICO_STACK_SIZE=4096
  PICO_FLASH_ASSUME_CORE1_SAFE=1
  PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=0
  PICO_STDIO_USB_STDOUT_TIMEOUT_US=20000)
pico_generate_pio_header(ceilometer ${CMAKE_CURRENT_LIST_DIR}/src/prn_tx.pio)
pico_generate_pio_header(ceilometer ${CMAKE_CURRENT_LIST_DIR}/src/test_pulse.pio)
target_link_libraries(ceilometer pico_stdlib hardware_pio hardware_pwm hardware_dma
  hardware_i2c hardware_spi hardware_adc hardware_vreg hardware_watchdog hardware_clocks hardware_sync hardware_flash pico_flash pico_rand)
pico_enable_stdio_usb(ceilometer 1)
pico_enable_stdio_uart(ceilometer 0)
# Explicitly reserve physical Pico 2 flash's final two sectors for settings.
if(NOT PICO_BOARD STREQUAL "pico2")
  message(FATAL_ERROR "Parameter flash layout is qualified for Pico 2 (4 MiB) only")
endif()
# Restrict the linker only. pico_override_flash_size also changes the SDK's
# physical capacity check, which must still permit writes to the settings area.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ceilometer")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ceilometer/pico_flash_region.ld"
  "FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 4186112\n")
pico_add_linker_script_override_path(ceilometer
  "${CMAKE_CURRENT_BINARY_DIR}/ceilometer" FILES pico_flash_region.ld)
pico_add_extra_outputs(ceilometer)
