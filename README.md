# OpenOBSW

**Open-source spacecraft On-Board Software stack**

OpenOBSW is a C11 OBSW implementing PUS-C services, b-dot detumbling, ADCS PD attitude control, and FDIR. Validated on MSP430FR5969 hardware, x86_64 host simulation, aarch64 QEMU, and ZynqMP Renode emulation.

---

## Validated Targets

| Target | Architecture | Transport | Validation |
|---|---|---|---|
| `obsw_msp430` | MSP430X (FR5969) | UCA0 UART | ✅ Hardware (LaunchPad) |
| `obsw_sim` | x86_64 (host) | stdin/stdout pipe | ✅ CI + SVF SIL |
| `obsw_sim_aarch64` | aarch64 (ZynqMP PS) | stdin/stdout pipe via QEMU | ✅ QEMU user-mode |
| `obsw_zynqmp` | aarch64 bare-metal | Cadence UART (0xFF000000) | ✅ Renode ZynqMP |

---

## Quick Start

```bash
git clone https://github.com/lipofefeyt/openobsw
cd openobsw
source scripts/setup-workspace.sh

omkbuild        # host sim (x86_64)
omkctest        # 17/17 C tests

omkbuild-aarch64     # aarch64 Linux (QEMU)
omksim-aarch64       # run under QEMU

baremetal-build      # aarch64 bare-metal (Renode)
renode renode/zynqmp_obsw.resc   # start Renode
python3 renode/test_ping_zynqmp.py  # validate ping
```

---

## Architecture

```
openobsw
├── src/
│   ├── aocs/
│   │   ├── bdot.c          B-dot detumbling (SAFE mode)
│   │   └── adcs.c          PD attitude control (NOMINAL mode)
│   ├── fdir/
│   │   └── fsm.c           Mode FSM (SAFE ↔ NOMINAL)
│   ├── pus/
│   │   ├── s1.c            TC verification
│   │   ├── s3.c            HK reporting
│   │   ├── s5.c            Event reporting + FDIR
│   │   ├── s6.c            Memory management
│   │   ├── s8.c            Function management
│   │   ├── s17.c           Test (ping/pong)
│   │   └── s20.c           Parameter get/set
│   └── hal/
│       ├── msp430/uart.c   MSP430FR5969 UCA0 UART HAL
│       └── zynqmp/uart.c   ZynqMP Cadence UART HAL (0xFF000000)
├── sim/
│   └── main.c              Host sim (stdin/stdout pipe protocol v3)
├── src/platform/
│   └── zynqmp/
│       ├── main.c          ZynqMP bare-metal entry point
│       ├── startup.S       AArch64 minimal startup (_start, BSS clear)
│       ├── zynqmp.ld       Linker script (0x400000, separate RX/RW)
│       └── libc_shim.c     memcpy/memset/strlen/sqrtf/acosf stubs
├── renode/
│   ├── zynqmp_obsw.resc    Renode ZynqMP platform script
│   └── test_ping_zynqmp.py S17 ping test via Renode socket
├── srdb/                   Spacecraft Resource Database
└── cmake/
    ├── msp430-toolchain.cmake
    ├── aarch64-linux-gnu.cmake   Linux cross-compiler
    └── aarch64-none-elf.cmake    Bare-metal toolchain (Renode)
```

---

## Wire Protocol v3

```
SVF/host → OBSW stdin (or UART):
  [0x01][uint16 BE len][TC frame]          TC uplink
  [0x02][uint16 BE len][sensor_frame_t]    MAG/GYRO/ST injection

OBSW → SVF stdout (or UART):
  [0x04][uint16 BE len][TM packet]         PUS TM downlink
  [0x03][uint16 BE len][actuator_frame_t]  Dipoles / RW torques
  [0xFF]                                   End of tick

OBSW stderr/UART (startup):
  [OBSW] ZynqMP started (type-frame protocol v2).
  [OBSW] SRDB version: 0.1.0
```

---

## Renode ZynqMP Emulation

The ZynqMP target runs the real C11 OBSW on Renode's emulated Cortex-A53:

```
test_ping_zynqmp.py                    Renode v1.15
  TCP socket ──────────────────────► ZynqMP platform
                                       zynqmp.repl:
                                         4x Cortex-A53 (APU)
                                         Cadence UART @ 0xFF000000
                                         Full DDR map
                                       obsw_zynqmp ELF loaded at 0x400000
                                       startup.S → main.c
                                       Cadence UART HAL (register access)
```

**Validated:** TC(17,1) → TM(1,1) acceptance + TM(17,2) pong + TM(1,7) completion

This is stronger than QEMU user-mode — the UART HAL exercises real Cadence UART register writes that Renode intercepts and emulates.

---

## PUS Services

| Service | Status |
|---|---|
| S1 TC Verification | ✅ |
| S3 HK Reporting | ✅ |
| S5 Event Reporting + FDIR | ✅ |
| S6 Memory Management | ✅ |
| S8 Function Management | ✅ |
| S17 Are-You-Alive | ✅ |
| S20 Parameter Get/Set | ✅ |

---

## Toolchains

| Target | Toolchain | Install |
|---|---|---|
| Host sim | System GCC | pre-installed |
| MSP430 | msp430-elf-gcc 9.3 | `nix-env -iA nixpkgs.msp430-elf-gcc` |
| aarch64 Linux | aarch64-unknown-linux-gnu-gcc | `nix-env -iA nixpkgs.pkgsCross.aarch64-multiplatform.stdenv.cc` |
| aarch64 bare-metal | aarch64-none-elf-gcc 12.3 | Download from developer.arm.com |

---

## Roadmap

| Milestone | Status |
|---|---|
| v0.1–v0.4 — Core PUS stack | ✅ Done |
| v0.5 — FDIR, AOCS, MSP430 hardware | ✅ Done |
| v0.6 — ZynqMP aarch64 SIL + Renode | ✅ Done |
| v0.7 — Full SVF closed loop via Renode socket | 📋 Planned |

---

## Related Projects

| Project | Role |
|---|---|
| [opensvf](https://github.com/lipofefeyt/opensvf) | Python SVF: validates openobsw via SIL |
| [opensvf-kde](https://github.com/lipofefeyt/opensvf-kde) | C++ 6-DOF physics engine (FMI 2.0) |

---

## License

Apache 2.0