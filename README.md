# OpenOBSW

**Open-source spacecraft On-Board Software stack**

OpenOBSW is a C11 OBSW implementing PUS-C services, b-dot detumbling, ADCS PD attitude control, and FDIR. Validated on MSP430FR5969 hardware and aarch64 (ZynqMP) via QEMU emulation.

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
│   │   ├── s5.c            Event reporting + FDIR trigger
│   │   ├── s8.c            Function management
│   │   └── s17.c           Test (ping/pong)
│   └── hal/
│       └── msp430/
│           └── uart.c      MSP430FR5969 UCA0 UART HAL
├── sim/
│   ├── main.c              Host simulation harness (pipe protocol v3)
│   └── sensor_inject.h     Sensor/actuator frame definitions
├── srdb/                   Spacecraft Resource Database
│   ├── data/               YAML parameter/event/TC definitions
│   ├── hardware/           Equipment hardware profiles
│   └── obsw_srdb/          Python loader + codegen + CSV I/O
├── cmake/
│   ├── msp430-toolchain.cmake
│   └── aarch64-linux-gnu.cmake
└── test/                   Unity C test suites (17 tests)
```

---

## Validated Targets

| Target | Architecture | Toolchain | Validation |
|---|---|---|---|
| `obsw_msp430` | MSP430X (FR5969) | msp430-elf-gcc 9.3 | Hardware (LaunchPad) |
| `obsw_sim` | x86_64 (host) | system GCC | CI + SVF SIL |
| `obsw_sim_aarch64` | aarch64 (ZynqMP PS) | aarch64-linux-gnu-gcc 12.3 | QEMU user-mode |

---

## Wire Protocol (obsw_sim ↔ SVF)

Protocol v3 — all frames type-prefixed:

```
SVF → obsw_sim stdin:
  [0x01][uint16 BE len][TC frame]          TC uplink
  [0x02][uint16 BE len][sensor_frame_t]    MAG/GYRO/ST injection

obsw_sim → SVF stdout:
  [0x04][uint16 BE len][TM packet]         PUS TM downlink
  [0x03][uint16 BE len][actuator_frame_t]  Dipoles / RW torques
  [0xFF]                                   End of tick

obsw_sim stderr (startup):
  [OBSW] Host sim started (type-frame protocol v2).
  [OBSW] SRDB version: 0.1.0
```

### Mode-aware AOCS

```
FSM SAFE    + MAG valid  → b-dot  → mtq_dipole[3]  Am²
FSM NOMINAL + ST + GYRO  → ADCS PD → rw_torque[3] Nm
```

---

## Quick Start

```bash
git clone https://github.com/lipofefeyt/openobsw
cd openobsw
source scripts/setup-workspace.sh

# Build host sim (x86_64)
omkbuild
omkctest        # 17/17 tests passing

# Build for aarch64 (ZynqMP)
omkbuild-aarch64

# Run under QEMU
omksim-aarch64
```

---

## SRDB

The Spacecraft Resource Database defines all parameters, events, telecommands and HK sets:

```bash
# Export to CSV for review
obsw-srdb-export --data-dir srdb/data --output-dir srdb/export

# Regenerate C header
obsw-srdb-codegen --data-dir srdb/data --output include/obsw/srdb_generated.h
```

Hardware profiles in `srdb/data/hardware/`:

| Profile | Type |
|---|---|
| `rw_default` | Reaction wheel |
| `rw_sinclair_rw003` | Sinclair RW-0.03 |
| `mtq_default` | Magnetorquer |
| `mag_default` | Magnetometer |
| `gyro_default` | Gyroscope |
| `thr_default` | Cold gas thruster |
| `thr_moog_monarc_1` | Moog Monarc-1 hydrazine |
| `gps_default` | Generic GPS |
| `gps_novatel_oem7` | NovAtel OEM7 |
| `thermal_default` | 3-node thermal |

---

## PUS Services

| Service | Status | Description |
|---|---|---|
| S1 | ✅ | TC verification (acceptance + completion) |
| S3 | ✅ | HK parameter reporting |
| S5 | ✅ | Event reporting + FDIR safe trigger |
| S6 | ✅ | Memory management |
| S8 | ✅ | Function management (recover nominal) |
| S17 | ✅ | Are-you-alive ping/pong |
| S20 | ✅ | Parameter get/set |

---

## Related Projects

| Project | Role |
|---|---|
| [opensvf](https://github.com/lipofefeyt/opensvf) | Python SVF: validates openobsw via SIL |
| [opensvf-kde](https://github.com/lipofefeyt/opensvf-kde) | C++ 6-DOF physics engine (FMI 2.0) |

---

## Roadmap

| Milestone | Status |
|---|---|
| v0.1–v0.4 — Core PUS stack | ✅ Done |
| v0.5 — FDIR, AOCS, MSP430 hardware | ✅ Done |
| v0.6 — ZynqMP aarch64 SIL | 🔄 In progress |
| v0.7 — Renode ZynqMP emulation | 📋 Planned |

### v0.6 Open Issues
- #18 Cadence UART HAL for ZynqMP PS
- #19 Renode ZynqMP platform script
- #14 MSP430 HAL UART driver (for hardware flashing)
- #17 Flash to MSP430FR5969 LaunchPad

---

## License

Apache 2.0

---

*Sister projects: [opensvf](https://github.com/lipofefeyt/opensvf) · [opensvf-kde](https://github.com/lipofefeyt/opensvf-kde)*