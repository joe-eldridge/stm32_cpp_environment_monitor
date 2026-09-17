cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME stm32_cpp_environment_monitor)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM, C and C++ languages
enable_language(C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/app.cpp
    Core/Src/time_sync.cpp
    Core/Src/i2c_fault_injection.cpp
    Core/Src/low_power.cpp
    Drivers/BSP/Components/ds3231/ds3231.cpp
    Drivers/BSP/Components/bme280/bme280.cpp
    Drivers/BSP/Components/veml7700/veml7700.cpp
    Drivers/BSP/Components/util/i2c_bus.cpp
    Drivers/BSP/Components/util/spi_bus.cpp
    Drivers/BSP/Components/sdcard/sdcard.cpp
    Drivers/BSP/Components/sdcard/sdcard_diskio.cpp
    Drivers/BSP/Components/display/ssd1681.cpp
    Drivers/BSP/Components/graphics/mono_framebuffer.cpp
)

# C++ hot-path/bare-metal build: no exception unwind tables, no RTTI
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Drivers/BSP/Components/ds3231
    Drivers/BSP/Components/bme280
    Drivers/BSP/Components/veml7700
    Drivers/BSP/Components/util
    Drivers/BSP/Components/sdcard
    Drivers/BSP/Components/display
    Drivers/BSP/Components/graphics
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
