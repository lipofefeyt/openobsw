# CMake toolchain file for ZynqMP bare-metal (aarch64-none-elf)
# Produces statically linked ELF suitable for Renode bare-metal loading.
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-none-elf.cmake \
#         -DOBSW_BUILD_ZYNQMP=ON ...

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Toolchain
# Try PATH first, fall back to known install location
find_program(CMAKE_C_COMPILER
    NAMES aarch64-none-elf-gcc
    PATHS /tmp/arm-gnu-toolchain-12.3.rel1-x86_64-aarch64-none-elf/bin
    NO_DEFAULT_PATH
)
if(NOT CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER aarch64-none-elf-gcc)
endif()
find_program(CMAKE_CXX_COMPILER
    NAMES aarch64-none-elf-g++
    PATHS /tmp/arm-gnu-toolchain-12.3.rel1-x86_64-aarch64-none-elf/bin
    NO_DEFAULT_PATH
)
find_program(CMAKE_ASM_COMPILER
    NAMES aarch64-none-elf-gcc
    PATHS /tmp/arm-gnu-toolchain-12.3.rel1-x86_64-aarch64-none-elf/bin
    NO_DEFAULT_PATH
)

# Bare-metal flags
set(CMAKE_C_FLAGS_INIT
    "-mcpu=cortex-a53 -ffreestanding -nostdlib -nostartfiles"
)

# No OS — skip compiler checks that require working executable
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Search paths
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
