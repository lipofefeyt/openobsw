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

# Extract toolchain
sudo tar xjf msp430-gcc-9.3.1.11_linux64.tar.bz2 -C /opt

# Install support files:
#   Headers (.h)  → toolchain include/
#   Linker scripts (.ld) → toolchain msp430-elf/lib/
unzip msp430-gcc-support-files-1.212.zip
find msp430-gcc-support-files -name "*.h"  -exec sudo cp {} /opt/msp430-gcc-9.3.1.11_linux64/include/ \;
find msp430-gcc-support-files -name "*.ld" -exec sudo cp {} /opt/msp430-gcc-9.3.1.11_linux64/msp430-elf/lib/ \;

# Add to PATH
export PATH=/opt/msp430-gcc-9.3.1.11_linux64/bin:$PATH

# Verify
msp430-elf-gcc --version
ls /opt/msp430-gcc-9.3.1.11_linux64/msp430-elf/lib/msp430fr5969.ld
```

Also install the MSP430 MCU headers and linker scripts:

```bash
sudo apt-get install -y binutils-msp430 msp430mcu
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

Run the MSP430 build in Renode without physical hardware:

```bash
# Build the MSP430 ELF first (requires MSP430 toolchain — see above)
cmake -B build-msp430 -DCMAKE_TOOLCHAIN_FILE=cmake/msp430-toolchain.cmake \
    -S targets/msp430-fr5969 -DRENODE_BUILD=ON
cmake --build build-msp430

# Terminal 1: launch Renode
renode renode/msp430fr5969.resc

# Terminal 2: send a TC(17,1) ping and verify TM(1,1) + TM(17,2) + TM(1,7)
python3 renode/test_ping.py
```

Expected output:

```
Connecting to Renode UART on localhost:3456...
Connected. Sending TC(17,1) ping...
  TM(1,1) — acceptance ✓
  TM(17,2) — are-you-alive pong ✓
  TM(1,7) — completion ✓

SUCCESS — TM(1,1) + TM(17,2) + TM(1,7) received ✓
```

The `RENODE_BUILD` flag switches the UART HAL from polled UCA0 registers to
interrupt-driven RX via the `MSP430_USCIA` Renode model (base 0x05C0, IE at
0x05D8, IFG at 0x05DA). The model's TCP terminal is exposed on port 3456.

## VS Code IntelliSense setup

IntelliSense for MSP430 files (`src/hal/msp430/`, `targets/msp430-fr5969/`)
requires `build-msp430/compile_commands.json` to exist. This is generated
at build time and is not committed. Run the MSP430 build once per workspace:

```bash
mkdebug-msp430 && mkbuild-msp430
```

Then in VS Code:

1. Open any MSP430 file (e.g. `src/hal/msp430/uart.c`)
2. Click the configuration name in the bottom-right status bar
3. Select **"MSP430"**
4. Run **Ctrl+Shift+P → C/C++: Reset IntelliSense Database** if squiggles persist

For host files, keep the configuration set to **"Host"**.

Both `compile_commands.json` files are regenerated automatically on each
rebuild — no manual steps needed after the first setup.