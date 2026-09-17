# Host-side unit tests for the hardware-independent parts of the firmware:
# sensor drivers (against a fake I2C device), timing helpers and pure logic.
# Built with the host compiler - separate from the firmware's cross build.
#
#   cmake -S tests -B build/tests
#   cmake --build build/tests
#   ctest --test-dir build/tests --output-on-failure
cmake_minimum_required(VERSION 3.22)
project(environment_monitor_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # for clangd, see ../.clangd

option(ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" ON)
option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
    URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(FIRMWARE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(COMPONENTS ${FIRMWARE_ROOT}/Drivers/BSP/Components)

add_library(project_warnings INTERFACE)
target_compile_options(project_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow
    $<$<BOOL:${WARNINGS_AS_ERRORS}>:-Werror>
)

add_library(project_sanitizers INTERFACE)
if(ENABLE_SANITIZERS)
    target_compile_options(project_sanitizers INTERFACE
        -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    target_link_options(project_sanitizers INTERFACE -fsanitize=address,undefined)
endif()

# The same driver sources the firmware builds, unmodified.
add_library(drivers STATIC
    ${COMPONENTS}/bme280/bme280.cpp
    ${COMPONENTS}/ds3231/ds3231.cpp
    ${COMPONENTS}/veml7700/veml7700.cpp
    ${COMPONENTS}/display/ssd1681.cpp
    ${COMPONENTS}/input/rotary_encoder.cpp
    ${COMPONENTS}/input/button.cpp
    ${COMPONENTS}/graphics/mono_framebuffer.cpp
    ${COMPONENTS}/graphics/text.cpp
    ${COMPONENTS}/graphics/font_5x7.cpp
    ${FIRMWARE_ROOT}/Core/Src/text_format.cpp
    ${FIRMWARE_ROOT}/Core/Src/main_screen.cpp
    ${FIRMWARE_ROOT}/Core/Src/hourly_history.cpp
    ${FIRMWARE_ROOT}/Core/Src/trend_screen.cpp
    ${FIRMWARE_ROOT}/Core/Src/menu.cpp
    ${FIRMWARE_ROOT}/Core/Src/menu_screen.cpp
)
target_include_directories(drivers PUBLIC
    ${COMPONENTS}/util
    ${COMPONENTS}/bme280
    ${COMPONENTS}/ds3231
    ${COMPONENTS}/veml7700
    ${COMPONENTS}/sdcard
    ${COMPONENTS}/display
    ${COMPONENTS}/graphics
    ${COMPONENTS}/input
    ${FIRMWARE_ROOT}/Core/Inc
)
target_link_libraries(drivers PRIVATE project_warnings project_sanitizers)

add_executable(unit_tests
    support/fake_hal.cpp
    test_bcd.cpp
    test_bme280.cpp
    test_button.cpp
    test_csd.cpp
    test_ds3231.cpp
    test_epaper_display.cpp
    test_fat_time.cpp
    test_hourly_history.cpp
    test_main_screen.cpp
    test_menu.cpp
    test_menu_screen.cpp
    test_mono_framebuffer.cpp
    test_rotary_encoder.cpp
    test_ssd1681.cpp
    test_text.cpp
    test_text_format.cpp
    test_timeout.cpp
    test_trend_screen.cpp
    test_veml7700.cpp
)
target_include_directories(unit_tests PRIVATE support)
target_link_libraries(unit_tests PRIVATE drivers GTest::gtest_main project_warnings project_sanitizers)

# Renders screens to image files for checking layouts; not run by ctest.
add_executable(screen_preview screen_preview.cpp)
target_link_libraries(screen_preview PRIVATE drivers project_warnings project_sanitizers)

enable_testing()
include(GoogleTest)
gtest_discover_tests(unit_tests)
