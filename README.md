# openobsw

Open-source on-board software core for satellite applications.

A portable, zero-allocation TT&C middleware stack in C11, designed to run on
bare metal, FreeRTOS, or RTEMS — and to integrate directly with
[OpenSVF](https://github.com/lipofefeyt/opensvf) for closed-loop system
validation against equipment models.

**Validated on real hardware**: S17 ping/pong confirmed on MSP430FR5969 LaunchPad.
**Validated in simulation**: Full PUS-C TC/TM pipeline exercised end-to-end via OpenSVF.

## Quick start

```bash
git clone https://github.com/lipofefeyt/openobsw
cd openobsw
bash scripts/setup-workspace.sh   # installs all dependencies
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
- **stdin**: `[uint16 BE length][TC frame bytes]`
- **stdout**: `[uint16 BE length][TM packet bytes]` then `[0xFF]` sync byte

This is the interface consumed by the OpenSVF `OBCEmulatorAdapter`.

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
| `hal/msp430/uart` | USCI_A0 UART driver (FR5969 LaunchPad backchannel) |

**SRDB** — YAML mission database with Pydantic validation and CMake-driven C header codegen.

## Test coverage

```
15 C suites / 97 tests / 0 failures / 0 warnings
```

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
| v0.5 — S6/S8, MSP430 build, hardware validation, OpenSVF integration | ✅ |
| v0.6 — Renode MSP430 emulation, ZynqMP target | 🔜 |

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