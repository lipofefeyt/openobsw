# openobsw

Open-source on-board software core for satellite applications.

A portable, zero-allocation TT&C middleware stack in C11, designed to run on
bare metal, FreeRTOS, or RTEMS — and to integrate directly with
[OpenSVF](https://github.com/lipofefeyt/opensvf) for closed-loop system
validation against equipment models.

The reference target is the **MSP430FR5969 LaunchPad** — the same hardware
can be validated under Renode emulation and then flashed and run on the board
without changing a line of code.

## Quick start (host build)

```bash
git clone https://github.com/lipofefeyt/openobsw
cd openobsw
pip install pydantic pyyaml --break-system-packages
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Requirements: `gcc`, `cmake >= 3.16`, `git`, `python3 >= 3.9`, `pydantic >= 2`, `pyyaml`.
Unity is fetched automatically by CMake.

### Recommended aliases

```bash
alias omkdebug='cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON'
alias omkbuild='cmake --build build'
alias otest='ctest --test-dir build --output-on-failure'
alias oclean='rm -rf build'
```

## MSP430 cross-compilation

See [docs/msp430-build.md](docs/msp430-build.md) for toolchain installation.
Once installed:

```bash
cmake -B build-msp430 \
  -DCMAKE_TOOLCHAIN_FILE=$(pwd)/cmake/msp430-toolchain.cmake \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -S targets/msp430-fr5969
cmake --build build-msp430
# outputs: build-msp430/openobsw-msp430.{elf,hex,map}
```

## What's in the box

**CCSDS / framing**

| Module | Description | Standard |
|---|---|---|
| `ccsds/space_packet` | Space Packet primary header encode/decode | CCSDS 133.0-B |
| `ccsds/tc_frame` | TC Transfer Frame decode, CRC-16/CCITT | CCSDS 232.0-B |
| `ccsds/tm_frame` | TM Transfer Frame build, idle fill, FECF | CCSDS 132.0-B |

**TC/TM stack**

| Module | Description |
|---|---|
| `tc/dispatcher` | Zero-alloc static routing table, handler + responder API |
| `tm/store` | Fixed-size ring buffer, store-and-forward |
| `hal/io` | Platform I/O vtable (`uint16_t` len, portable to 16-bit targets) |

**PUS-C services**

| Module | Description | Standard |
|---|---|---|
| `pus/s1` | TC verification: acceptance, completion ack/nak | PUS-C S1 |
| `pus/s3` | Housekeeping: static parameter sets, periodic tick | PUS-C S3 |
| `pus/s5` | Event reporting: four severity levels, FDIR coupling | PUS-C S5 |
| `pus/s6` | Memory management: load, check (CRC-16), dump | PUS-C S6 |
| `pus/s8` | Function management: static table, recover_nominal | PUS-C S8 |
| `pus/s17` | Are-you-alive ping/pong | PUS-C S17 |

**FDIR**

| Module | Description |
|---|---|
| `fdir/fsm` | Safe mode FSM: NOMINAL ↔ SAFE, hooks, TC whitelist |
| `fdir/watchdog` | Software watchdog, kick API, expiry callback |
| `hal/arm/trap_table` | ARM Cortex-A exception stubs (ZynqMP) |
| `hal/msp430/trap_table` | MSP430 fault stubs |
| `hal/msp430/uart` | USCI_A1 UART driver (FR5969 LaunchPad backchannel) |

**SRDB**

| Component | Description |
|---|---|
| `srdb/data/*.yaml` | Mission database: parameters, TCs, HK sets, events |
| `srdb/obsw_srdb/` | Python loader + Pydantic validation + C header codegen |
| `build/include/obsw/srdb_generated.h` | Generated at build time — named constants for C |

See [docs/architecture.md](docs/architecture.md) for full design documentation.
See [docs/mission-config.md](docs/mission-config.md) for mission-specific parameters.
See [docs/msp430-build.md](docs/msp430-build.md) for the MSP430 target build guide.

## Test coverage

```
15 C suites / 97 tests / 0 failures / 0 warnings
42 Python tests / 0 failures
```

| Suite | Tests | Type |
|---|---|---|
| test_space_packet | 8 | unit |
| test_dispatcher | 6 | unit |
| test_tm_store | 6 | unit |
| test_tc_frame | 10 | unit |
| test_tm_frame | 9 | unit |
| test_s1 | 6 | unit |
| test_s3 | 10 | unit |
| test_s5 | 5 | unit |
| test_s6 | 8 | unit |
| test_s8 | 5 | unit |
| test_s17 | 5 | unit |
| test_fsm | 11 | unit |
| test_watchdog | 9 | unit |
| test_s5_fdir | 5 | unit |
| test_tc_pipeline | 2 | integration |

## Host simulation

```bash
python3 sim/send_ping.py | ./build/sim/obsw_sim
# [OBSW] Host sim started (SCID=0x001). Awaiting TC frames on stdin.
# ACK apid=0x010 svc=17 subsvc=1 flags=0x09 seq=1
```

## VS Code IntelliSense

Add `.vscode/c_cpp_properties.json` with:
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

| Milestone | Content | Status |
|---|---|---|
| v0.1 | Space Packet + TC dispatcher + TM store + host sim | ✅ |
| v0.2 | TC Frame + TM Frame + CRC-16 | ✅ |
| v0.3 | PUS-C S1, S3, S5, S17 | ✅ |
| v0.4 | FDIR: FSM, watchdog, trap stubs | ✅ |
| SRDB | YAML database, Python loader, CMake codegen | ✅ |
| v0.5 phase 1 | S6 memory mgmt, S8 function mgmt | ✅ |
| v0.5 phase 2 | MSP430 toolchain, FR5969 target, ELF + HEX output | ✅ |
| v0.5 phase 3 | Renode MSP430 platform + sentinel peripheral | 🔜 |
| v0.5 phase 4 | OpenSVF OBCEmulatorAdapter | 🔜 |

## Standards references

| Standard | Title |
|---|---|
| CCSDS 133.0-B | Space Packet Protocol |
| CCSDS 232.0-B | TC Space Data Link Protocol |
| CCSDS 132.0-B | TM Space Data Link Protocol |
| ECSS-E-ST-70-41C | Packet Utilisation Standard (PUS-C) |

## License

Apache 2.0