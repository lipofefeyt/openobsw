# OpenOBSW

**Open-source spacecraft On-Board Software stack — C11, PUS-C, validated on real hardware**

[![CI](https://github.com/lipofefeyt/openobsw/actions/workflows/ci.yml/badge.svg)](https://github.com/lipofefeyt/openobsw/actions/workflows/ci.yml)

OpenOBSW is a flight software stack for small satellites. It implements the PUS-C service protocol, b-dot detumbling, ADCS PD attitude control, and FDIR — in portable C11 with zero dynamic allocation. It runs on MSP430FR5969 hardware, STM32H750VBT6 hardware, x86_64 host simulation, aarch64 QEMU, and ZynqMP Renode emulation. It is validated by [OpenSVF](https://github.com/lipofefeyt/opensvf) in closed-loop SIL against a 6-DOF physics engine.

---

## Validated targets

| Target | Architecture | Transport | Status |
|---|---|---|---|
| `obsw_msp430` | MSP430X (FR5969) | UCA0 UART 9600 baud | ✅ Hardware (LaunchPad) |
| `obsw_sim` | x86_64 (host) | stdin/stdout pipe | ✅ CI + SVF SIL |
| `obsw_sim_aarch64` | aarch64 (ZynqMP PS) | stdin/stdout pipe via QEMU | ✅ QEMU user-mode |
| `obsw_zynqmp` | aarch64 bare-metal | Cadence UART @ 0xFF000000 | ✅ Renode ZynqMP |
| `obsw_stm32h7` | Cortex-M7 (STM32H750) | USART3 PD8/PD9 | ✅ Hardware validated (ST-Link SWD) |

---

## Quick start

```bash
git clone https://github.com/lipofefeyt/openobsw
cd openobsw

# Install dependencies (Ubuntu/Debian)
sudo apt-get install -y cmake ninja-build gcc clang \
    gcc-arm-none-eabi python3-full python3-venv

# Set up Python venv
python3 -m venv .venv && source .venv/bin/activate
pip install -e srdb/

# Build host sim and run tests
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
    -DOBSW_BUILD_TESTS=ON -DOBSW_BUILD_SIM=ON -G Ninja
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Or with the dev container (recommended):

```bash
# Open in VS Code dev container — all toolchains pre-installed
source scripts/activate.sh   # load aliases
host-build                   # build host sim
host-test                    # run 18 C unit tests
```

```
100% tests passed, 0 tests failed out of 18
```

18 unit tests + 1 integration test cover TC/TM framing, all PUS services, FDIR state machine, watchdog, and AOCS algorithms.

---

## Architecture

```
openobsw/
├── src/
│   ├── pus/
│   │   ├── s1.c      TC verification (TM 1,1/2/3/7/8)
│   │   ├── s3.c      Housekeeping reporting
│   │   ├── s5.c      Event reporting + FDIR triggers
│   │   ├── s6.c      Memory management (load/check/dump)
│   │   ├── s8.c      Function management (recover_nominal)
│   │   ├── s17.c     Are-you-alive ping/pong
│   │   └── s20.c     On-board parameter get/set
│   ├── aocs/
│   │   ├── bdot.c    B-dot detumbling (SAFE mode)
│   │   └── adcs.c    PD attitude control (NOMINAL mode)
│   ├── fdir/
│   │   ├── fsm.c     Mode FSM (SAFE ↔ NOMINAL)
│   │   └── watchdog.c  Software watchdog
│   ├── hal/
│   │   ├── msp430/uart.c   MSP430FR5969 UCA0 UART
│   │   ├── stm32h7/uart.c  STM32H750 USART3 UART
│   │   └── zynqmp/uart.c   ZynqMP Cadence UART (register-level)
│   └── platform/
│       ├── stm32h7/
│       │   ├── main.c      STM32H750 entry point
│       │   ├── startup.S   Cortex-M7 minimal startup
│       │   └── stm32h750.ld Linker script
│       └── zynqmp/
│           ├── main.c      ZynqMP bare-metal entry point
│           ├── startup.S   AArch64 minimal startup
│           └── zynqmp.ld   Linker script (load at 0x400000)
├── sim/
│   └── main.c        Host sim — wire protocol v3 over stdin/stdout
├── renode/
│   ├── zynqmp_obsw.resc     Renode ZynqMP platform script
│   └── test_ping_zynqmp.py  TC(17,1) ping validation via socket
└── srdb/
    └── data/
        ├── parameters.yaml  TM parameter definitions (ID, type, limits)
        └── spacecraft.yaml  APID, modes, default gains
```

See [docs/architecture.md](docs/architecture.md) for full module descriptions, data flow diagrams, and AOCS algorithm documentation.

---

## PUS-C services

| Service | Subservices | Notes |
|---|---|---|
| S1 TC Verification | TM(1,1) accept, TM(1,2) fail, TM(1,7) complete, TM(1,8) fail | All TC handlers emit S1 reports |
| S3 HK Reporting | TC(3,1) define, TC(3,5/6) enable/disable, TM(3,25) report | Essential sets auto-enabled at boot |
| S5 Event Reporting | TM(5,1–4) info/low/medium/high | High-severity events trigger safe mode |
| S6 Memory Management | TC(6,2) load, TC(6,5) check, TC(6,9) dump | CRC-16/CCITT verification |
| S8 Function Management | TC(8,1) perform | Function ID 1 = recover to NOMINAL |
| S17 Are-You-Alive | TC(17,1) ping, TM(17,2) pong | Used as heartbeat by SVF and YAMCS |
| S20 Parameter Management | TC(20,1) set, TC(20,3) get, TM(20,2) report | Static parameter table, zero allocation |

---

## Wire protocol v3

The same protocol runs over stdin/stdout (host sim), QEMU pipe, and Renode socket:

```
Host/SVF → OBSW:
  [0x01][uint16 BE len][PUS TC bytes]       TC uplink
  [0x02][uint16 BE len][sensor_frame_t]     MAG/GYRO/ST sensor injection

OBSW → Host/SVF:
  [0x03][uint16 BE len][actuator_frame_t]   MTQ dipoles + RW torques
  [0x04][uint16 BE len][PUS TM bytes]       TM downlink
  [0xFF]                                    End of tick (sync byte)
```

`sensor_frame_t` (47 bytes, packed, little-endian): MAG xyz + valid, ST quaternion + valid, GYRO xyz + valid, sim_time.

`actuator_frame_t` (29 bytes, packed, little-endian): MTQ dipoles xyz, RW torques xyz, controller byte, sim_time.

---

## Renode ZynqMP emulation

```bash
# Terminal 1
renode renode/zynqmp_obsw.resc

# Terminal 2 — validated: TM(1,1) + TM(17,2) + TM(1,7)
python3 renode/test_ping_zynqmp.py
```

OpenSVF connects to the same Renode socket and runs full closed-loop campaigns against the ZynqMP binary.

---

## STM32H750 hardware validation

Tested on WeAct STM32H750VBT6 board via ST-Link V2 SWD:

```bash
# Attach ST-Link to WSL2 (PowerShell admin on Windows)
usbipd bind --busid <busid>
usbipd attach --wsl --busid <busid>

# Flash via OpenOCD (inside dev container)
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
    -c "program build_stm32h7/obsw_stm32h7.bin 0x08000000 verify reset exit"
```

UART on USART3 PD8/PD9 at 115200 baud (HSI 64MHz, BRR=555). PLL init (480MHz) is written but disabled — debugging in progress via ST-Link GDB.

---

## MSP430 hardware validation

Tested on MSP430FR5969 LaunchPad:

- UCA0 UART (not UCA1) at 9600 baud, 1MHz MCLK
- `PM5CTL0 &= ~LOCKLPM5` to unlock GPIO
- Watchdog disabled at startup

```bash
MSPFlasher -i USB -w build_msp430/obsw_msp430.hex -v -z [VCC]
```

---

## Toolchains

| Target | Toolchain | Install |
|---|---|---|
| Host sim | System GCC / Clang | `apt install gcc clang` |
| MSP430 | msp430-elf-gcc 9.3.1 | TI toolchain installer |
| aarch64 Linux | aarch64-linux-gnu-gcc | `apt install gcc-aarch64-linux-gnu` |
| aarch64 bare-metal | aarch64-none-elf-gcc 12.3 | [ARM Developer](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) |
| STM32H7 | arm-none-eabi-gcc | `apt install gcc-arm-none-eabi` |

The dev container installs all toolchains automatically via `post-create.sh`.

---

## Local development (WSL2 + Dev Containers)

```bash
# Windows (PowerShell admin)
wsl --install -d Ubuntu-24.04
winget install -e --id Docker.DockerDesktop --source winget
winget install -e --id dorssel.usbipd-win --source winget

# WSL2
git clone https://github.com/lipofefeyt/openobsw ~/workspace/openobsw
code ~/workspace/openobsw   # VS Code → Reopen in Container
```

---

## Roadmap

| Version | Status |
|---|---|
| v0.1–v0.4 — Core PUS stack (S1, S3, S5, S17), CCSDS framing | ✅ Done |
| v0.5 — FDIR, b-dot, ADCS PD, MSP430 hardware validation | ✅ Done |
| v0.6 — ZynqMP aarch64 SIL, S6/S8, SRDB, Renode emulation | ✅ Done |
| v0.7 — S20 parameter management, SVF closed-loop via Renode socket | ✅ Done |
| v0.8 — STM32H750 PLL clock init, USB CDC, MSP430 Renode emulation | 🔄 In progress |
| v0.9 — Dual-OBC topology (ZynqMP + MSP430 lockstep) | 🔄 Planned |

---

## Related projects

| Project | Role |
|---|---|
| [opensvf](https://github.com/lipofefeyt/opensvf) | Python SVF: validates openobsw in closed-loop SIL |
| [opensvf-kde](https://github.com/lipofefeyt/opensvf-kde) | C++ 6-DOF kinematics and dynamics engine (FMI 2.0) |

---

## License

Apache 2.0

*Built by [lipofefeyt](https://github.com/lipofefeyt)*