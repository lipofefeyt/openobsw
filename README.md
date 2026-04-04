# openobsw

Open-source on-board software core for satellite applications.

A portable, zero-allocation TT&C and AOCS middleware stack in C11, designed
to run on bare metal, FreeRTOS, or RTEMS — and to integrate directly with
[OpenSVF](https://github.com/lipofefeyt/opensvf) for closed-loop system
validation against equipment models.

**Validated on real hardware**: S17 ping/pong confirmed on MSP430FR5969 LaunchPad.
**Closed-loop AOCS**: B-dot detumbling (safe mode) + PD attitude control (nominal).
**OpenSVF integrated**: full TC/TM pipeline exercised end-to-end via binary pipe.

## Quick start

```bash
git clone https://github.com/lipofefeyt/openobsw
cd openobsw
bash scripts/setup-workspace.sh
source ~/.bashrc
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## MSP430 cross-compilation

```bash
mkdebug-msp430 && mkbuild-msp430
# outputs: build-msp430/openobsw-msp430.{elf,hex,map}
```

See [docs/msp430-build.md](docs/msp430-build.md) for toolchain installation.

## Host simulation (OpenSVF integration)

```bash
cmake --build build
python3 sim/send_ping.py
```

The host sim (`build/sim/obsw_sim`) speaks a binary pipe protocol:

```
stdin:  [0x01][uint16 BE len][TC frame]     — TC uplink
        [0x02][uint16 BE len][sensor frame] — sensor injection (sim-only)
stdout: [uint16 BE len][TM packet]          — TM downlink
        [0xFF]                              — sync byte (end of cycle)
```

See [docs/architecture.md](docs/architecture.md) for protocol details.

## What's in the box

**CCSDS / framing**

| Module | Standard |
|---|---|
| `ccsds/space_packet` | CCSDS 133.0-B |
| `ccsds/tc_frame` | CCSDS 232.0-B |
| `ccsds/tm_frame` | CCSDS 132.0-B |

**PUS-C services**

| Service | Description |
|---|---|
| S1 | TC verification: acceptance, completion ack/nak |
| S3 | Housekeeping: static parameter sets, periodic tick |
| S5 | Event reporting: four severity levels, FDIR coupling |
| S6 | Memory management: load, check (CRC-16), dump |
| S8 | Function management: static table, recover_nominal |
| S17 | Are-you-alive ping/pong |

**FDIR**

| Module | Description |
|---|---|
| `fdir/fsm` | Safe mode FSM: NOMINAL ↔ SAFE, hooks, TC whitelist |
| `fdir/watchdog` | Software watchdog, kick API, expiry callback |

**AOCS**

| Module | Description |
|---|---|
| `aocs/bdot` | B-dot detumbling controller (safe mode) |
| `aocs/adcs` | PD attitude controller + quaternion math (nominal mode) |

**HAL**

| Module | Description |
|---|---|
| `hal/msp430/uart` | USCI_A0 UART driver (FR5969 LaunchPad backchannel) |
| `hal/arm/trap_table` | ARM Cortex-A exception stubs |
| `hal/msp430/trap_table` | MSP430 fault stubs |

**SRDB** — YAML mission database with Pydantic validation and CMake-driven C header codegen.

## Test coverage

```
17 C suites / 116 tests / 0 failures / 0 warnings
```

| Suite | Tests | Description |
|---|---|---|
| test_space_packet | 8 | CCSDS Space Packet |
| test_dispatcher | 6 | TC dispatcher routing |
| test_tm_store | 6 | TM ring buffer |
| test_tc_frame | 10 | TC Transfer Frame |
| test_tm_frame | 9 | TM Transfer Frame |
| test_s1 | 6 | PUS-C S1 verification |
| test_s3 | 10 | PUS-C S3 housekeeping |
| test_s5 | 5 | PUS-C S5 events |
| test_s6 | 8 | PUS-C S6 memory mgmt |
| test_s8 | 5 | PUS-C S8 function mgmt |
| test_s17 | 5 | PUS-C S17 ping |
| test_fsm | 11 | Safe mode FSM |
| test_watchdog | 9 | Software watchdog |
| test_s5_fdir | 5 | S5 ↔ FSM coupling |
| test_tc_pipeline | 2 | Integration |
| test_bdot | 6 | B-dot controller |
| test_adcs | 10 | PD controller + quaternion math |

## Workspace setup

After cloning or workspace reset:

```bash
bash scripts/setup-workspace.sh
source ~/.bashrc
```

## VS Code IntelliSense

```json
{
    "configurations": [
        { "name": "Host",   "compileCommands": "${workspaceFolder}/build/compile_commands.json" },
        { "name": "MSP430", "compileCommands": "${workspaceFolder}/build-msp430/compile_commands.json" }
    ],
    "version": 4
}
```

## Roadmap

| Milestone | Status |
|---|---|
| v0.1 — Space Packet + TC dispatcher + TM store | ✅ |
| v0.2 — TC Frame + TM Frame + CRC-16 | ✅ |
| v0.3 — PUS-C S1, S3, S5, S17 | ✅ |
| v0.4 — FDIR: FSM, watchdog, trap stubs | ✅ |
| SRDB — YAML database, Python loader, CMake codegen | ✅ |
| v0.5 — S6/S8, MSP430 hardware validation, OpenSVF integration | ✅ |
| v0.6 — AOCS: B-dot + PD controller, sensor injection protocol | 🔄 |
| v0.7 — ZynqMP BSP, FreeRTOS tasks, Renode emulation | 🔜 |

## Standards references

| Standard | Title |
|---|---|
| CCSDS 133.0-B | Space Packet Protocol |
| CCSDS 232.0-B | TC Space Data Link Protocol |
| CCSDS 132.0-B | TM Space Data Link Protocol |
| ECSS-E-ST-70-41C | Packet Utilisation Standard (PUS-C) |
| ECSS-E-TM-10-21A | System validation via simulation |

## License

Apache 2.0