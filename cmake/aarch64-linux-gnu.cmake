# CMake toolchain file for ZynqMP Cortex-A53 (aarch64 Linux)
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Cross compiler: crosstool-ng name first, then the Debian/Ubuntu package
# name (gcc-aarch64-linux-gnu, used in CI)
find_program(CMAKE_C_COMPILER NAMES aarch64-unknown-linux-gnu-gcc aarch64-linux-gnu-gcc REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES aarch64-unknown-linux-gnu-g++ aarch64-linux-gnu-g++)

# Don't try to run target binaries on host during cmake checks
set(CMAKE_CROSSCOMPILING_EMULATOR qemu-aarch64)

# Search paths
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
