# openobsw — MSP430FR5969 Build Guide

## Overview

openobsw supports cross-compilation for the MSP430FR5969 LaunchPad
(safe-mode OBC target). The MSP430 build profile uses reduced buffer
sizes to fit within the 2KB SRAM constraint.

| Resource | Host build | MSP430 profile |
|---|---|---|
| TM store | 32 slots × 1024 bytes = 32KB | 4 slots × 64 bytes = 256 bytes |
| TC frame max | 1024 bytes | 128 bytes |
| S5 safe triggers | 8 | 4 |

## Toolchain installation (once per workspace)

TI's msp430-elf-gcc is not available in Ubuntu package repos.
Install it manually from TI:

```bash
# Download TI MSP430 GCC 9.3.1.11 and support files
wget "http://software-dl.ti.com/msp430/msp430_public_sw/mcu/msp430/MSPGCC/9_3_1_2/export/msp430-gcc-9.3.1.11_linux64.tar.bz2"
wget "http://software-dl.ti.com/msp430/msp430_public_sw/mcu/msp430/MSPGCC/9_3_1_2/export/msp430-gcc-support-files-1.212.zip"

# Extract to /opt
sudo tar xjf msp430-gcc-9.3.1.11_linux64.tar.bz2 -C /opt

# Install support files (headers + linker scripts) into the toolchain
unzip msp430-gcc-support-files-1.212.zip
sudo cp -r msp430-gcc-support-files/include/* /opt/msp430-gcc-9.3.1.11_linux64/include/

# Add to PATH (add to ~/.bashrc for persistence)
export PATH=/opt/msp430-gcc-9.3.1.11_linux64/bin:$PATH

# Verify
msp430-elf-gcc --version
```

Also install the MSP430 MCU headers and linker scripts:

```bash
sudo apt-get install -y binutils-msp430 msp430mcu
```

### Gitpod persistence

Add to `.gitpod.yml` to install automatically on workspace start:

```yaml
tasks:
  - init: |
      sudo apt-get install -y binutils-msp430 msp430mcu python3-pip
      pip install pydantic pyyaml
      wget -q "http://software-dl.ti.com/msp430/msp430_public_sw/mcu/msp430/MSPGCC/9_3_1_2/export/msp430-gcc-9.3.1.11_linux64.tar.bz2"
      wget -q "http://software-dl.ti.com/msp430/msp430_public_sw/mcu/msp430/MSPGCC/9_3_1_2/export/msp430-gcc-support-files-1.212.zip"
      sudo tar xjf msp430-gcc-9.3.1.11_linux64.tar.bz2 -C /opt
      unzip -q msp430-gcc-support-files-1.212.zip
      sudo cp -r msp430-gcc-support-files/include/* /opt/msp430-gcc-9.3.1.11_linux64/include/
      echo 'export PATH=/opt/msp430-gcc-9.3.1.11_linux64/bin:$PATH' >> ~/.bashrc
```

## Building

```bash
cmake -B build-msp430 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/msp430-toolchain.cmake \
  -S targets/msp430-fr5969

cmake --build build-msp430
```

Outputs in `build-msp430/`:
- `openobsw-msp430.elf` — ELF image (for Renode and debugging)
- `openobsw-msp430.hex` — Intel HEX (for flashing to hardware)
- `openobsw-msp430.map` — symbol map

## Flashing to hardware

Using mspdebug with the FR5969 LaunchPad connected via USB:

```bash
sudo apt-get install -y mspdebug
mspdebug tilib "prog build-msp430/openobsw-msp430.hex"
```

Or using TI UniFlash (GUI tool, Windows/Linux).

## Memory layout (FR5969)

```
FRAM (code + rodata + data init):  0x4400 – 0xFF7F  (47 KB)
SRAM (stack + bss + data):         0x2000 – 0x27FF   (2 KB)
IVT (interrupt vectors):           0xFF80 – 0xFFFF  (128 B)
```

Stack size: 512 bytes (configurable in `targets/msp430-fr5969/linker.ld`).

## Debugging with GDB

```bash
# Terminal 1: start mspdebug GDB server
mspdebug tilib gdb

# Terminal 2: connect GDB
msp430-elf-gdb build-msp430/openobsw-msp430.elf
(gdb) target remote :2000
(gdb) load
(gdb) continue
```

## Renode emulation

See `renode/README.md` for running the MSP430 build under Renode
without physical hardware.