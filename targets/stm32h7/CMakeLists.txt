cmake_minimum_required(VERSION 3.14)

# Build with:
#   cmake -B build_stm32h7 \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/stm32h7-toolchain.cmake \
#     -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build_stm32h7

project(openobsw_stm32h7 C ASM)

# Force usage of ARM toolchain
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_FLAGS "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -x assembler-with-cpp")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ── SRDB code generation ──────────────────────────────────────────────
set(SRDB_GENERATED_H "${CMAKE_BINARY_DIR}/include/obsw/srdb_generated.h")
add_custom_command(
    OUTPUT  ${SRDB_GENERATED_H}
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_BINARY_DIR}/include/obsw"
    COMMAND ${Python3_EXECUTABLE} -m obsw_srdb.codegen
            --data-dir "${OBSW_ROOT}/srdb/data"
            --output   "${SRDB_GENERATED_H}"
    DEPENDS "${OBSW_ROOT}/srdb/data/parameters.yaml"
            "${OBSW_ROOT}/srdb/data/spacecraft.yaml"
    COMMENT "Generating SRDB header"
)
add_custom_target(srdb_codegen_stm32h7 DEPENDS ${SRDB_GENERATED_H})

add_library(obsw_srdb_generated_stm32h7 INTERFACE)
add_dependencies(obsw_srdb_generated_stm32h7 srdb_codegen_stm32h7)
target_include_directories(obsw_srdb_generated_stm32h7
    INTERFACE "${CMAKE_BINARY_DIR}/include"
)

# ── OBSW core library ─────────────────────────────────────────────────
add_library(obsw_stm32h7 STATIC
    ${OBSW_ROOT}/src/ccsds/space_packet.c
    ${OBSW_ROOT}/src/ccsds/tc_frame.c
    ${OBSW_ROOT}/src/ccsds/tm_frame.c
    ${OBSW_ROOT}/src/tc/dispatcher.c
    ${OBSW_ROOT}/src/tm/store.c
    ${OBSW_ROOT}/src/pus/pus_tm.c
    ${OBSW_ROOT}/src/pus/s1.c
    ${OBSW_ROOT}/src/pus/s17.c
    ${OBSW_ROOT}/src/pus/s20.c
    ${OBSW_ROOT}/src/hal/stm32h7/uart.c
    ${OBSW_ROOT}/sim/sensor_inject.c
)

target_include_directories(obsw_stm32h7 PUBLIC
    ${OBSW_ROOT}/include
    ${OBSW_ROOT}/sim
)

target_link_libraries(obsw_stm32h7 PUBLIC obsw_srdb_generated_stm32h7)

set_target_properties(obsw_stm32h7 PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

# ── STM32H750 binary ──────────────────────────────────────────────────
set(LINKER_SCRIPT "${OBSW_ROOT}/src/platform/stm32h7/stm32h750.ld")

add_executable(obsw_stm32h7_elf
    ${OBSW_ROOT}/src/platform/stm32h7/startup.S
    ${OBSW_ROOT}/src/platform/stm32h7/main.c
)

target_link_libraries(obsw_stm32h7_elf PRIVATE obsw_stm32h7)

target_include_directories(obsw_stm32h7_elf PRIVATE
    ${CMAKE_BINARY_DIR}/include
    ${OBSW_ROOT}/include
    ${OBSW_ROOT}/sim
)

target_link_options(obsw_stm32h7_elf PRIVATE
    -T${LINKER_SCRIPT}
    -Wl,-Map=${CMAKE_BINARY_DIR}/obsw_stm32h7.map
)

set_target_properties(obsw_stm32h7_elf PROPERTIES
    OUTPUT_NAME "obsw_stm32h7"
    SUFFIX ".elf"
)

# Generate .bin for DFU flashing
add_custom_command(TARGET obsw_stm32h7_elf POST_BUILD
    COMMAND arm-none-eabi-objcopy -O binary
            $<TARGET_FILE:obsw_stm32h7_elf>
            ${CMAKE_BINARY_DIR}/obsw_stm32h7.bin
    COMMAND arm-none-eabi-size $<TARGET_FILE:obsw_stm32h7_elf>
    COMMENT "Generating obsw_stm32h7.bin for DFU"
)

message(STATUS "STM32H750 target configured")
message(STATUS "  Flash: ${LINKER_SCRIPT}")
message(STATUS "  Flash binary: ${CMAKE_BINARY_DIR}/obsw_stm32h7.bin")
message(STATUS "  DFU: dfu-util -a 0 -s 0x08000000:leave -D build_stm32h7/obsw_stm32h7.bin")