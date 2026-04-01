# cmake/msp430-toolchain.cmake
# CMake toolchain file for MSP430 cross-compilation with TI's msp430-elf-gcc.
#
# Usage:
#   cmake -B build-msp430 \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/msp430-toolchain.cmake \
#     -DOBSW_TARGET=msp430-fr5969
#
# Toolchain installation (run once in Gitpod):
#   wget http://software-dl.ti.com/msp430/msp430_public_sw/mcu/msp430/MSPGCC/9_3_1_2/export/msp430-gcc-9.3.1.11_linux64.tar.bz2
#   wget http://software-dl.ti.com/msp430/msp430_public_sw/mcu/msp430/MSPGCC/9_3_1_2/export/msp430-gcc-support-files-1.212.zip
#   sudo tar xjf msp430-gcc-9.3.1.11_linux64.tar.bz2 -C /opt
#   unzip msp430-gcc-support-files-1.212.zip
#   sudo cp -r msp430-gcc-support-files/include/* /opt/msp430-gcc-9.3.1.11_linux64/include/
#   echo 'export PATH=/opt/msp430-gcc-9.3.1.11_linux64/bin:$PATH' >> ~/.bashrc
#   source ~/.bashrc

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR msp430)

# Locate the toolchain — search common install locations
find_program(MSP430_GCC msp430-elf-gcc
    PATHS
        /opt/msp430-gcc-9.3.1.11_linux64/bin
        /usr/local/bin
        /usr/bin
    REQUIRED
)
get_filename_component(MSP430_TOOLCHAIN_BIN ${MSP430_GCC} DIRECTORY)
set(MSP430_TOOLCHAIN_BIN "${MSP430_TOOLCHAIN_BIN}" CACHE PATH "MSP430 toolchain bin directory" FORCE)

set(CMAKE_C_COMPILER   "${MSP430_TOOLCHAIN_BIN}/msp430-elf-gcc")
set(CMAKE_CXX_COMPILER "${MSP430_TOOLCHAIN_BIN}/msp430-elf-g++")
set(CMAKE_AR           "${MSP430_TOOLCHAIN_BIN}/msp430-elf-ar")
set(CMAKE_RANLIB       "${MSP430_TOOLCHAIN_BIN}/msp430-elf-ranlib")
set(CMAKE_OBJCOPY      "${MSP430_TOOLCHAIN_BIN}/msp430-elf-objcopy")
set(CMAKE_SIZE         "${MSP430_TOOLCHAIN_BIN}/msp430-elf-size")

# FR5969 target CPU
set(MSP430_MCU "msp430fr5969" CACHE STRING "MSP430 MCU target")

# Include support files — prefer TI toolchain headers over system msp430mcu package
# The msp430mcu package (/usr/msp430/include) ships older headers that conflict
# with TI msp430-elf-gcc. Use the toolchain's own include directory.
execute_process(
    COMMAND "${MSP430_GCC}" -print-sysroot
    OUTPUT_VARIABLE MSP430_SYSROOT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(MSP430_MCU_INCLUDE "${MSP430_TOOLCHAIN_BIN}/../msp430-elf/include"
    CACHE PATH "MSP430 MCU include path" FORCE)

set(CMAKE_C_FLAGS_INIT
    "-mmcu=${MSP430_MCU} -mlarge -mdata-region=none -mcode-region=none"
)

# Don't try to run target binaries on host
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# sysroot
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)