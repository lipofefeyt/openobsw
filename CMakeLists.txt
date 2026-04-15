cmake_minimum_required(VERSION 3.16)
project(openobsw VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Build options
option(OBSW_BUILD_TESTS  "Build unit tests"        ON)
option(OBSW_BUILD_SIM    "Build host simulation"   ON)
option(OBSW_ENABLE_ASAN  "Enable AddressSanitizer" OFF)

# Strict warnings
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wformat=2 -Wundef -Wshadow
    -Wcast-align -Wcast-qual
    -Wmissing-prototypes -Wstrict-prototypes
)

if(OBSW_ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(srdb)
add_subdirectory(src)

if(OBSW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

if(OBSW_BUILD_SIM)
    add_subdirectory(sim)
endif()
# ------------------------------------------------------------------ #
# ZynqMP bare-metal target (aarch64)                                  #
# ------------------------------------------------------------------ #
option(OBSW_BUILD_ZYNQMP "Build ZynqMP bare-metal target" OFF)

if(OBSW_BUILD_ZYNQMP)
    add_executable(obsw_zynqmp
        src/hal/zynqmp/uart.c
        src/platform/zynqmp/main.c
    )
    target_link_libraries(obsw_zynqmp PRIVATE obsw obsw_srdb_generated m)
    target_include_directories(obsw_zynqmp PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
    )
    target_compile_definitions(obsw_zynqmp PRIVATE
        ZYNQMP_BUILD
        ZYNQMP_UART_BAUD_INIT
    )
endif()
