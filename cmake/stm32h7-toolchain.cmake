# cmake/stm32h7-toolchain.cmake
# CMake toolchain file for STM32H750 cross-compilation with arm-none-eabi-gcc.
#
# Usage:
#   cmake -B build_stm32h7 \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/stm32h7-toolchain.cmake \
#     -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build_stm32h7
#
# Flash via DFU (WeAct STM32H750):
#   Hold BOOT0, press RESET, then:
#   dfu-util -a 0 -s 0x08000000:leave -D build_stm32h7/obsw_stm32h7.bin
#   (or use STM32CubeProgrammer on Windows)

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_AR           arm-none-eabi-ar)
set(CMAKE_RANLIB       arm-none-eabi-ranlib)
set(CMAKE_OBJCOPY      arm-none-eabi-objcopy)
set(CMAKE_SIZE         arm-none-eabi-size)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# STM32H750VBT6 — Cortex-M7, FPU, 128KB internal flash, 1MB RAM
set(CPU_FLAGS
    "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard"
)

set(CMAKE_C_FLAGS_INIT
    "${CPU_FLAGS} -fdata-sections -ffunction-sections -Wall -Wextra"
)
set(CMAKE_ASM_FLAGS_INIT
    "${CPU_FLAGS} -x assembler-with-cpp"
)
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "${CPU_FLAGS} -Wl,--gc-sections -Wl,--print-memory-usage -specs=nano.specs -specs=nosys.specs -Wl,--no-warn-rwx-segments"
)