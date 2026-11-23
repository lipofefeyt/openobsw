# OpenOBSW

**Open-source, flight-oriented spacecraft on-board software: C11, PUS-C, no dynamic allocation**

[![CI](https://github.com/lipofefeyt/openobsw/actions/workflows/ci.yml/badge.svg)](https://github.com/lipofefeyt/openobsw/actions/workflows/ci.yml)
[![Docs](https://readthedocs.org/projects/openobsw/badge/?version=latest)](https://openobsw.readthedocs.io/)

**Documentation: [openobsw.readthedocs.io](https://openobsw.readthedocs.io/)**: targets,
wire protocol (ICD), SRDB, requirements and known limits, one version per release.

OpenOBSW is a small, permissively licensed PUS-C flight-software stack that
runs unchanged from 16-bit microcontrollers to 64-bit SoCs, and comes with an
open validation facility, [OpenSVF](https://github.com/lipofefeyt/opensvf).

It implements CCSDS space packets (and TC/TM transfer frames as library
code that no target uses yet), the PUS-C services listed below, a
STANDBY/SAFE/NOMINAL mode machine with FDIR, B-dot detumbling and PD attitude
control, in portable C11 with no `malloc` and no global state in the core.
OpenSVF runs it closed loop (SIL) against a 6-DOF physics engine.

> **Status:** flight-oriented, not flight-proven. It has not flown. It has a
> requirements baseline with test traceability, MISRA C:2012 static analysis
> and a coverage gate in CI. What each target supports, and what shows it, is
> on the [Targets](https://openobsw.readthedocs.io/en/latest/targets/) page;
> what it does not do is on [Known limits](https://openobsw.readthedocs.io/en/latest/known-limits/).

---

## Targets

| Target | Architecture | Transport | Tier |
|---|---|---|---|
| `obsw_sim` | x86_64 Linux | stdin/stdout pipe | 1: supported, in CI on every PR (build, tests, ASAN/UBSAN, coverage) |
| `obsw_sim` (aarch64) | aarch64 Linux | pipe via QEMU user-mode | 1: supported, in CI on every PR (build, tests under QEMU) |
| `obsw_stm32h7`, FreeRTOS | Cortex-M7 (STM32H750) | USART3 115200 | 1: supported, built in CI on every PR; tested in Renode and on a WeAct board, locally |
| `obsw_stm32h7`, superloop | Cortex-M7 (STM32H750) | USART3 115200 | 2: builds in CI, bench-tested once |
| `obsw_msp430` | MSP430FR5969, 2 KB SRAM | UCA0 UART 9600, own framing | 2: builds in CI on every PR, bench-tested once |
| `obsw_zynqmp` | aarch64 bare metal | Cadence UART | 3: builds in CI, emulator only |

Per-feature support and its evidence: [Targets](https://openobsw.readthedocs.io/en/latest/targets/).
Not yet shown: the B-dot loop closed on real hardware (#52); STM32H7 as a
closed-loop SIL target (#54). The STM32H750 runs on the 64 MHz HSI, with PLL
init to 480 MHz written but disabled (#72).

---

## PUS-C services

A handler only acts in a build whose dispatcher routes a TC to it. Routed TCs
per target:

| Service | Host sim | STM32H7 FreeRTOS | ZynqMP | STM32H7 superloop | MSP430 |
|---|---|---|---|---|---|
| S1 TC verification | every TC; rejections as TM(1,2) | same | same | same | same |
| S3 housekeeping | 3,1 3,3 3,5 3,6 | 3,1 3,3 3,5 3,6 |  |  | 3,5 3,6 |
| S5 event reporting | TM only | TM only |  |  | TM only |
| S6 memory management | 6,2 6,5 6,9 | 6,2 6,5 6,9 |  |  |  |
| S8 function management | 8,1 | 8,1 | 8,1 |  | 8,1 |
| S9 time management | 9,128 | 9,128 |  |  |  |
| S11 time-based scheduling | 11,4–11,6 11,17 11,18 | 11,4–11,6 11,17 11,18 |  |  |  |
| S12 on-board monitoring | 12,1–12,5 | 12,1–12,5 |  |  |  |
| S19 event-action | 19,1–19,5 | 19,1–19,5 |  |  |  |
| S17 are-you-alive | 17,1 | 17,1 | 17,1 | 17,1 | 17,1 |
| S20 parameter management | 20,1 20,3 | 20,1 20,3 |  | 20,1 20,3 |  |

| Service | Subservices | Notes |
|---|---|---|
| S1 | TM(1,1) accept, TM(1,2) fail, TM(1,7) complete, TM(1,8) fail | TM(1,2) codes: `0x0002` unknown service, `0x0003` unknown subservice, `0x0005` bad data, `0x0006` S6 address, `0x0007` not allowed in this mode |
| S3 | TC(3,1) define, TC(3,3) delete, TC(3,5/6) enable/disable, TM(3,25) report | Parameters big-endian; TC(3,1) builds sets from S20 parameter IDs, TC(3,3) removes them |
| S5 | TM(5,1–4) info/low/medium/high | Events on the SRDB safe-trigger list command SAFE |
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
source scripts/activate.sh   # venv + aliases; obsw-help prints docs/cheatsheet.md
host-build                   # build host sim
host-test                    # run the C tests
```

`activate.sh` picks the venv for where you are: `.venv` in the dev container
and on plain Linux, `.venv-wsl` in native WSL2. The two share one working
tree and a venv only works where it was created, so each side keeps its own
(`scripts/env.sh`); `setup-workspace.sh` creates the right one.

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

- C unit and integration tests under ctest (Unity).
- System tests: `pytest test/system` drives the built `obsw_sim` over wire protocol v4 (set `OBSW_SIM` to another binary).
- SRDB Python tests: `pytest srdb/tests`.
- Requirements: [docs/SRS.md](docs/SRS.md), with the matrix on the [Traceability](https://openobsw.readthedocs.io/en/latest/traceability/) page. Every test names the requirements it verifies (`/* @verifies OBSW-SRS-NNN */` in C, `@pytest.mark.requirement` in Python). `python3 tools/trace.py --lib build/src/libobsw.a --out build/traceability.md` checks that every implemented requirement is tested and writes the matrix; CI runs it on every PR.
- Static analysis: CI runs cppcheck with the MISRA C:2012 addon on the core library and fails on any finding outside the documented Advisory deviations ([docs/misra-deviations.md](docs/misra-deviations.md)).
- Coverage: CI fails if unit, integration and system tests together cover less than 90% of core library lines (the CI job prints the figures). Locally: configure with `-DOBSW_ENABLE_COVERAGE=ON` (GCC), run the tests, then `gcovr -r . --object-directory build --filter 'src/(ccsds|tc|tm|pus|fdir|aocs|time)/'`.

---

## Architecture

```
openobsw/
├── src/
│   ├── ccsds/       space packet; TC/TM transfer frames (library only, unused by targets)
│   ├── tc/          TC dispatcher: static routes, acceptance gate
│   ├── tm/          TM store: fixed ring buffer
│   ├── pus/         S1 S3 S5 S6 S8 S9 S11 S12 S17 S19 S20
│   ├── fdir/        mode FSM (STANDBY/SAFE/NOMINAL), watchdog
│   ├── aocs/        B-dot with dB/dt high-pass filter; PD quaternion control
│   ├── time/        CUC-4,2 on-board time
│   ├── task/        FreeRTOS tasks: tmtc, pus, aocs, fdir, mode
│   ├── hal/         UART, I2C, QMC5883L, LCD per target
│   └── platform/    STM32H7 and ZynqMP entry points, startup, linker scripts
├── targets/msp430-fr5969/   MSP430 build
├── sim/             host sim: wire protocol v4 over stdin/stdout
├── renode/          ZynqMP and STM32H7 Renode platforms and tests (run locally)
├── srdb/            Spacecraft Resource Database: YAML → C header + XTCE
├── docs/            documentation site sources (mkdocs, Read the Docs)
└── tools/           traceability check, docs generator
```

See [Architecture](https://openobsw.readthedocs.io/en/latest/architecture/),
[FreeRTOS](https://openobsw.readthedocs.io/en/latest/freertos-architecture/) and
[Mission configuration](https://openobsw.readthedocs.io/en/latest/mission-config/)
(values to set before flight).

---

## Wire protocol v4

The same framing runs over stdin/stdout (host sim), the QEMU pipe and the
target UARTs (Renode exposes them as a socket). MSP430 uses its own framing.
The full ICD, generated from `sim/sensor_inject.h`, is the
[Wire protocol](https://openobsw.readthedocs.io/en/latest/icd/) page.

```
Host/SVF → OBSW:
  [0x01][uint16 BE len][TC space packet]    TC uplink
  [0x02][uint16 BE len][sensor_frame_t]     MAG/GYRO/ST + OBC thermal injection

OBSW → Host/SVF:
  [0x03][uint16 BE len][actuator_frame_t]   MTQ dipoles + RW torques
  [0x04][uint16 BE len][TM space packet]    TM downlink
  [0xFF]                                    End of tick (sync byte)
```

`sensor_frame_t` (50 bytes, packed, little-endian): MAG xyz + valid, ST
quaternion + valid, GYRO xyz + valid, sim_time, OBC temperature (int16 SRDB
raw counts) + valid. The host sim exits on a sensor frame of any other length
(a v3 sender's 47 bytes, for one).

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

OpenSVF can connect to the same socket. Closed-loop AOCS on ZynqMP does not
work yet: the target stops answering in NOMINAL (#147).

### STM32H750 (WeAct board, ST-Link V2)

```bash
# WSL2: attach the ST-Link (PowerShell, admin)
usbipd bind --busid <busid>
usbipd attach --wsl --busid <busid>

# Build the FreeRTOS image and flash (dev container)
stm32h7-build
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
    -c "program build_stm32h7/obsw_stm32h7.bin 0x08000000 verify reset exit"
```

USART3 on PD8/PD9 at 115200 baud.

### MSP430FR5969 LaunchPad

UCA0 UART at 9600 baud, 1 MHz MCLK. See [docs/msp430-build.md](docs/msp430-build.md).

```bash
MSPFlasher -i USB -w build-msp430/openobsw-msp430.hex -v -z [VCC]
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
| v0.18 | Service parity with OpenSVF | S11 scheduling, S12 monitoring, S19 event-action (#79; S15 deferred, S23 out of scope); TC(3,3) delete (#91); repeated TC arguments in the SRDB (#92); HK layouts taken from the SRDB; MISRA 12.1 and 13.3 enforced | ✅ Done |
| v0.19 | Recovery evidence | Wire protocol v4 with an OBC thermal input (#122); every TM packet stamped with on-board time; S12 monitoring commands SAFE on STM32H7 FreeRTOS; SRDB safe-trigger list on every target; host sim mode-transition events; S12 event aux as float32; LEOP autonomy timer (#111); STM32H750 LCD dashboard | ✅ Done |
| v0.20 | Documentation | Documentation site on Read the Docs: generated ICD, SRDB, traceability and target capability pages, known limits (#134 to #139); aarch64 sim and STM32H7 FreeRTOS in CI; Renode STM32H750 TC path (#106) and S3 interval test (#51) | In progress |
| v0.21 | Hardware loop | B-dot HPF validation on opensvf-kde (#70); STM32H750 PLL at 480 MHz (#72); B-dot HIL and ICM-42688 gyro (#52, #53); STM32H750 Renode SIL (#54, #55); convergence harness (#67, #68); YAMCS-encodable XTCE commands (#108) | Next |
| v1.0 | QUBIK | STM32L476 HAL target; link-layer decision (OSDLP or proven interoperability); separate GPL mission build; HIL on a QUBIK COMMS board | Proposed |

Parked: dual-OBC (#29–#31), USB CDC (#27), INA219 (#39), ground CLI (#50),
AOCS step refactor (#40), BOM docs (#41). See [CHANGELOG.md](CHANGELOG.md).

---

## Related projects

| Project | Role |
|---|---|
| [opensvf](https://github.com/lipofefeyt/opensvf) | Python SVF: validates openobsw in closed-loop SIL |
| opensvf-kde (not public yet) | C++ 6-DOF kinematics and dynamics engine (FMI 2.0) |

---

## License

Apache 2.0

*Built by [lipofefeyt](https://github.com/lipofefeyt)*
