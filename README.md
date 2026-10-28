# OpenOBSW

**Open-source, flight-oriented spacecraft on-board software — C11, PUS-C, no dynamic allocation**

[![CI](https://github.com/lipofefeyt/openobsw/actions/workflows/ci.yml/badge.svg)](https://github.com/lipofefeyt/openobsw/actions/workflows/ci.yml)

OpenOBSW is a small, permissively licensed PUS-C flight-software stack that
runs unchanged from 16-bit microcontrollers to 64-bit SoCs, and comes with an
open validation facility, [OpenSVF](https://github.com/lipofefeyt/opensvf).

It implements CCSDS TC/TM framing, the PUS-C services listed below, a
STANDBY/SAFE/NOMINAL mode machine with FDIR, B-dot detumbling and PD attitude
control, in portable C11 with no `malloc` and no global state in the core.
OpenSVF runs it closed loop (SIL) against a 6-DOF physics engine.

> **Status:** flight-oriented, not flight-proven. It has not flown, and there
> is no requirements baseline, static analysis or coverage gate yet; those are
> the [v0.17 train](#roadmap). Hardware evidence is listed per target below.

---

## Targets

| Target | Architecture | Transport | Evidence | In CI |
|---|---|---|---|---|
| `obsw_sim` | x86_64 host | stdin/stdout pipe | Unit + integration tests; OpenSVF closed-loop SIL | build, tests, ASAN/UBSAN |
| `obsw_sim` (aarch64) | aarch64 Linux | pipe via QEMU user-mode | Runs under `qemu-aarch64` | — |
| `obsw_zynqmp` | aarch64 bare metal | Cadence UART | Renode: TC(17,1) ping, S8 mode changes, NOMINAL AOCS test | binary |
| `obsw_stm32h7` | Cortex-M7 (STM32H750), FreeRTOS | USART3 115200 | WeAct board: TC/TM ping over UART; Renode ping | binary |
| `obsw_msp430` | MSP430FR5969, 2 KB SRAM | UCA0 UART 9600 | LaunchPad: TC/TM ping over UART | binary + memory report, every PR |

Not yet shown: the B-dot loop closed on real hardware (#52); the STM32H750 runs
on the 64 MHz HSI, with PLL init to 480 MHz written but disabled (#72).

---

## PUS-C services

A handler only acts in a build whose dispatcher routes a TC to it. Routed TCs
per target:

| Service | Host sim | STM32H7 FreeRTOS | ZynqMP | STM32H7 superloop | MSP430 |
|---|---|---|---|---|---|
| S1 TC verification | every TC; rejections as TM(1,2) | same | same | same | same |
| S3 housekeeping | 3,1 3,3 3,5 3,6 | 3,1 3,3 3,5 3,6 | — | — | 3,5 3,6 |
| S5 event reporting | TM only | TM only | — | — | TM only |
| S6 memory management | 6,2 6,5 6,9 | 6,2 6,5 6,9 | — | — | — |
| S8 function management | 8,1 | 8,1 | 8,1 | — | 8,1 |
| S9 time management | 9,128 | 9,128 | — | — | — |
| S11 time-based scheduling | 11,4–11,6 11,17 11,18 | 11,4–11,6 11,17 11,18 | — | — | — |
| S12 on-board monitoring | 12,1–12,5 | 12,1–12,5 | — | — | — |
| S19 event-action | 19,1–19,5 | 19,1–19,5 | — | — | — |
| S17 are-you-alive | 17,1 | 17,1 | 17,1 | 17,1 | 17,1 |
| S20 parameter management | 20,1 20,3 | 20,1 20,3 | — | 20,1 20,3 | — |

| Service | Subservices | Notes |
|---|---|---|
| S1 | TM(1,1) accept, TM(1,2) fail, TM(1,7) complete, TM(1,8) fail | TM(1,2) codes: `0x0002` unknown service, `0x0003` unknown subservice, `0x0005` bad data, `0x0006` S6 address, `0x0007` not allowed in this mode |
| S3 | TC(3,1) define, TC(3,3) delete, TC(3,5/6) enable/disable, TM(3,25) report | Parameters big-endian; TC(3,1) builds sets from S20 parameter IDs, TC(3,3) removes them |
| S5 | TM(5,1–4) info/low/medium/high | High-severity events drive the FSM to SAFE |
| S6 | TC(6,2) load, TC(6,5) check, TC(6,9) dump | Region table with READ/WRITE access; fails closed |
| S8 | TC(8,1) perform | Function 1 recover NOMINAL, 2 SAFE, 3 STANDBY |
| S9 | TC(9,128) set OBT | Mission-specific subservice, CUC-4,2 |
| S11 | TC(11,4) insert, TC(11,5/6) delete, TC(11,17/18) enable/disable | Time-tagged TCs released at OBT through the dispatcher (SAFE gate applies at release) |
| S12 | TC(12,1/2) enable/disable, TC(12,3) add, TC(12,4/5) delete | Limit checks on S20 parameters each tick; S5 event on entering an out-of-limit state, latched until back in range |
| S19 | TC(19,1) add, TC(19,2/3) delete, TC(19,4/5) enable/disable | Event ID → stored TC; runs on the next tick through the dispatcher (SAFE gate and S1 reports apply). FreeRTOS reacts to S12 events |
| S17 | TC(17,1) ping, TM(17,2) pong | Heartbeat for OpenSVF and YAMCS |
| S20 | TC(20,1) set, TC(20,3) get, TM(20,2) report | Static parameter table |

**SAFE and STANDBY** accept only TC(17,1), (8,1), (9,128) and (20,3), on every
target. The list comes from `safe_allowed` in `srdb/data/telecommands.yaml`.

S15 (storage and retrieval) is deferred and S23 (file management) out of scope (#79).

---

## Quick start

```bash
git clone https://github.com/lipofefeyt/openobsw
cd openobsw

# Dev container (recommended): toolchains are installed by
# scripts/setup-workspace.sh. Elsewhere, run that script on Ubuntu/Debian.
source .venv/bin/activate
source scripts/activate.sh   # aliases
host-build                   # build host sim
host-test                    # run the C tests
```

Without the aliases:

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install -e "srdb/[dev]"   # SRDB codegen runs at build time
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
    -DOBSW_BUILD_TESTS=ON -DOBSW_BUILD_SIM=ON -G Ninja
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
pytest srdb/tests
```

### Tests and traceability

- 20 ctest suites (19 unit, 1 integration).
- System tests: `pytest test/system` drives the built `obsw_sim` over wire protocol v3 (set `OBSW_SIM` to another binary).
- SRDB Python tests: `pytest srdb/tests`.
- Requirements: [docs/SRS.md](docs/SRS.md). Every test names the requirements it verifies (`/* @verifies OBSW-SRS-NNN */` in C, `@pytest.mark.requirement` in Python). `python3 tools/trace.py --lib build/src/libobsw.a --out build/traceability.md` checks that every implemented requirement is tested and writes the matrix; CI runs it on every PR.
- Static analysis: CI runs cppcheck with the MISRA C:2012 addon on the core library and fails on any finding outside the documented Advisory deviations ([docs/misra-deviations.md](docs/misra-deviations.md)).
- Coverage: CI fails if unit, integration and system tests together cover less than 90% of core library lines (now 93.7% lines, 79.3% branches). Locally: configure with `-DOBSW_ENABLE_COVERAGE=ON` (GCC), run the tests, then `gcovr -r . --object-directory build --filter 'src/(ccsds|tc|tm|pus|fdir|aocs|time)/'`.

---

## Architecture

```
openobsw/
├── src/
│   ├── ccsds/       TC transfer frame (CRC-16), space packet, TM transfer frame
│   ├── tc/          TC dispatcher: static routes, acceptance gate
│   ├── tm/          TM store: fixed ring buffer
│   ├── pus/         S1 S3 S5 S6 S8 S9 S17 S20
│   ├── fdir/        mode FSM (STANDBY/SAFE/NOMINAL), watchdog
│   ├── aocs/        B-dot with dB/dt high-pass filter; PD quaternion control
│   ├── time/        CUC-4,2 on-board time
│   ├── task/        FreeRTOS tasks: tmtc, pus, aocs, fdir, mode
│   ├── hal/         UART, I2C, QMC5883L, LCD per target
│   └── platform/    STM32H7 and ZynqMP entry points, startup, linker scripts
├── targets/msp430-fr5969/   MSP430 build
├── sim/             host sim: wire protocol v3 over stdin/stdout
├── renode/          ZynqMP and STM32H7 Renode platforms and ping tests
└── srdb/            Spacecraft Resource Database: YAML → C header + XTCE
```

See [docs/architecture.md](docs/architecture.md),
[docs/freertos-architecture.md](docs/freertos-architecture.md) and
[docs/mission-config.md](docs/mission-config.md) (values to set before flight).

---

## Wire protocol v3

The same framing runs over stdin/stdout (host sim), the QEMU pipe and the
Renode socket:

```
Host/SVF → OBSW:
  [0x01][uint16 BE len][PUS TC bytes]       TC uplink
  [0x02][uint16 BE len][sensor_frame_t]     MAG/GYRO/ST sensor injection

OBSW → Host/SVF:
  [0x03][uint16 BE len][actuator_frame_t]   MTQ dipoles + RW torques
  [0x04][uint16 BE len][PUS TM bytes]       TM downlink
  [0xFF]                                    End of tick (sync byte)
```

`sensor_frame_t` (47 bytes, packed, little-endian): MAG xyz + valid, ST
quaternion + valid, GYRO xyz + valid, sim_time.

`actuator_frame_t` (29 bytes, packed, little-endian): MTQ dipoles xyz, RW
torques xyz, controller byte, sim_time.

PUS TC/TM content is big-endian, per PUS-C.

---

## Running on targets

### Renode ZynqMP

```bash
renode renode/zynqmp_obsw.resc        # terminal 1
python3 renode/test_ping_zynqmp.py    # terminal 2: TM(1,1) + TM(17,2) + TM(1,7)
```

OpenSVF connects to the same Renode socket for closed-loop campaigns.

### STM32H750 (WeAct board, ST-Link V2)

```bash
# WSL2: attach the ST-Link (PowerShell, admin)
usbipd bind --busid <busid>
usbipd attach --wsl --busid <busid>

# Build and flash (dev container)
stm32h7-build
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
    -c "program build_stm32h7/obsw_stm32h7.bin 0x08000000 verify reset exit"
```

USART3 on PD8/PD9 at 115200 baud.

### MSP430FR5969 LaunchPad

UCA0 UART at 9600 baud, 1 MHz MCLK. See [docs/msp430-build.md](docs/msp430-build.md).

```bash
MSPFlasher -i USB -w build_msp430/obsw_msp430.hex -v -z [VCC]
```

---

## Toolchains

| Target | Toolchain |
|---|---|
| Host sim | GCC or Clang |
| MSP430 | TI msp430-elf-gcc 9.3.1 |
| aarch64 Linux | aarch64-linux-gnu-gcc |
| aarch64 bare metal | aarch64-none-elf-gcc 12.3 |
| STM32H7 | arm-none-eabi-gcc |

`scripts/setup-workspace.sh` installs all of them; the dev container runs it
on creation.

---

## Roadmap

| Train | Theme | Contents | Status |
|---|---|---|---|
| v0.16 | Tell the truth | Tag the v0.12–v0.15 work; S6, TC(3,1) and S9 routed; rejected TCs reach TM(1,2) (#80); SAFE whitelist on every target (#86); HK big-endian (#82); SRDB tests and `-Werror` in CI; README rewrite | ✅ Done |
| v0.17 | Qualification baseline | SRS (78 requirements) with test traceability in CI; `obsw_sim` system tests; coverage gate ≥ 90% core lines; cppcheck + MISRA C:2012, Required rules at zero (#74); MSP430 build in CI | ✅ Done |
| v0.18 | Service parity with OpenSVF | S11/S12/S19 scope (#79); S15 and S23; S3 HK in the FreeRTOS PUS task (#51); TC(3,3) delete (#91); repeated TC arguments in the SRDB (#92) | Next |
| v0.19 | Hardware loop | B-dot HPF validation on opensvf-kde (#70); STM32H750 PLL at 480 MHz (#72); B-dot HIL and ICM-42688 gyro (#52, #53); STM32H750 Renode transport and SIL (#54, #55); convergence harness (#67, #68) | Planned |
| v1.0 | QUBIK | STM32L476 HAL target; link-layer decision (OSDLP or proven interoperability); separate GPL mission build; HIL on a QUBIK COMMS board | Proposed |

Parked: dual-OBC (#29–#31), USB CDC (#27), INA219 (#39), ground CLI (#50),
AOCS step refactor (#40), BOM docs (#41). See [CHANGELOG.md](CHANGELOG.md).

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
