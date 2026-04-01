# targets/msp430-fr5969/CMakeLists.txt
# MSP430 FR5969 build target for openobsw.
#
# This target produces:
#   openobsw-msp430.elf   — flashable ELF image
#   openobsw-msp430.hex   — Intel HEX for mspdebug / UniFlash
#
# Memory map (MSP430FR5969):
#   SRAM:  0x2000 – 0x27FF  (2 KB)
#   FRAM:  0x4400 – 0xFF7F  (47 KB, code + data)
#   IVT:   0xFF80 – 0xFFFF  (interrupt vector table)

cmake_minimum_required(VERSION 3.16)
project(openobsw_msp430 C ASM)

# ---- MSP430 profile compile flags ----------------------------------------

set(MSP430_CFLAGS
    -mmcu=${MSP430_MCU}
    -mlarge
    -mdata-region=none
    -mcode-region=none
    -O2
    -Wall -Wextra
    -ffunction-sections
    -fdata-sections
    -fno-exceptions
)

set(MSP430_LDFLAGS
    -mlarge
    -Wl,--gc-sections
    -Wl,-Map=openobsw-msp430.map
    -Wl,--defsym,__stack_size=0x200
)

# ---- MSP430 profile: reduced buffer sizes --------------------------------

# 2KB SRAM budget breakdown:
#   TM store:   4 slots × 64 bytes = 256 bytes
#   TC frame:   128 bytes max
#   Stack:      ~512 bytes
#   BSS/data:   ~512 bytes
#   Remaining:  ~768 bytes for handlers

set(MSP430_PROFILE_DEFS
    -DOBSW_TM_STORE_SLOTS=4
    -DOBSW_TM_MAX_PACKET_LEN=64
    -DOBSW_TC_FRAME_MAX_LEN=128
    -DOBSW_S5_MAX_SAFE_TRIGGERS=4
)

# ---- Source files --------------------------------------------------------

set(OBSW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

set(MSP430_SOURCES
    # CCSDS
    ${OBSW_ROOT}/src/ccsds/space_packet.c
    ${OBSW_ROOT}/src/ccsds/tc_frame.c
    ${OBSW_ROOT}/src/ccsds/tm_frame.c
    # TC/TM stack
    ${OBSW_ROOT}/src/tc/dispatcher.c
    ${OBSW_ROOT}/src/tm/store.c
    # PUS services
    ${OBSW_ROOT}/src/pus/pus_tm.c
    ${OBSW_ROOT}/src/pus/s1.c
    ${OBSW_ROOT}/src/pus/s3.c
    ${OBSW_ROOT}/src/pus/s5.c
    ${OBSW_ROOT}/src/pus/s6.c
    ${OBSW_ROOT}/src/pus/s8.c
    ${OBSW_ROOT}/src/pus/s17.c
    # FDIR
    ${OBSW_ROOT}/src/fdir/fsm.c
    ${OBSW_ROOT}/src/fdir/watchdog.c
    # MSP430 HAL
    ${OBSW_ROOT}/src/hal/msp430/uart.c
    # Application entry point
    main.c
)

# ---- SRDB generated header -----------------------------------------------

set(SRDB_GENERATED_H "${CMAKE_BINARY_DIR}/include/obsw/srdb_generated.h")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

file(GLOB SRDB_YAML_FILES "${OBSW_ROOT}/srdb/data/*.yaml")

add_custom_command(
    OUTPUT "${SRDB_GENERATED_H}"
    COMMAND "${CMAKE_COMMAND}" -E env
            "PYTHONPATH=${OBSW_ROOT}/srdb"
            "${Python3_EXECUTABLE}" -m obsw_srdb.codegen
            --data-dir "${OBSW_ROOT}/srdb/data"
            --output   "${SRDB_GENERATED_H}"
    DEPENDS ${SRDB_YAML_FILES}
    COMMENT "[srdb] Generating srdb_generated.h for MSP430 target"
    VERBATIM
)

add_custom_target(srdb_codegen_msp430 DEPENDS "${SRDB_GENERATED_H}")

# ---- ELF target ----------------------------------------------------------

add_executable(openobsw_msp430 ${MSP430_SOURCES})
add_dependencies(openobsw_msp430 srdb_codegen_msp430)

target_include_directories(openobsw_msp430 PRIVATE
    ${OBSW_ROOT}/include
    ${CMAKE_BINARY_DIR}/include
    ${MSP430_TOOLCHAIN_BIN}/../include
    ${MSP430_TOOLCHAIN_BIN}/../msp430-elf/include
)

target_compile_options(openobsw_msp430 PRIVATE
    ${MSP430_CFLAGS}
    ${MSP430_PROFILE_DEFS}
    -include "${SRDB_GENERATED_H}"
)

target_link_options(openobsw_msp430 PRIVATE
    ${MSP430_LDFLAGS}
)

set_target_properties(openobsw_msp430 PROPERTIES
    OUTPUT_NAME "openobsw-msp430"
    SUFFIX ".elf"
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

# ---- Post-build: generate HEX and print size -----------------------------

add_custom_command(TARGET openobsw_msp430 POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O ihex
            "$<TARGET_FILE:openobsw_msp430>"
            "${CMAKE_BINARY_DIR}/openobsw-msp430.hex"
    COMMAND "${CMAKE_SIZE}" "$<TARGET_FILE:openobsw_msp430>"
    COMMENT "[msp430] Generating HEX and printing size"
)