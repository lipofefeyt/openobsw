# openobsw

Open-source on-board software core for satellite applications.

A portable, zero-allocation TT&C middleware stack in C11, designed to run on
bare metal, FreeRTOS, or RTEMS — and to integrate directly with
[OpenSVF](https://github.com/lipofefeyt/opensvf) for closed-loop system
validation against equipment models.

## Quick start

```bash
git clone https://github.com/lipofefeyt/openobsw
cd openobsw
pip install pydantic pyyaml --break-system-packages
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Requirements: `gcc`, `cmake >= 3.16`, `git`, `python3`, `pydantic`, `pyyaml`.
Unity is fetched automatically by CMake.

### Recommended aliases

```bash
alias omkdebug='cmake -B build -DCMAKE_BUILD_TYPE=Debug'
alias omkbuild='cmake --build build'
alias otest='ctest --test-dir build --output-on-failure'
alias oclean='rm -rf build'
```

### Clean rebuild

Always required when `CMakeLists.txt` files change:

```bash
oclean && omkdebug && omkbuild && otest
```

## What's in the box

**CCSDS / framing**

| Module | Description | Standard |
|---|---|---|
| `ccsds/space_packet` | Space Packet primary header encode/decode, full packet parse | CCSDS 133.0-B |
| `ccsds/tc_frame` | TC Transfer Frame decode, CRC-16/CCITT validation | CCSDS 232.0-B |
| `ccsds/tm_frame` | TM Transfer Frame build, idle fill, FECF | CCSDS 132.0-B |

**TC/TM stack**

| Module | Description | Standard |
|---|---|---|
| `tc/dispatcher` | Zero-alloc static routing table, handler + responder API | ECSS PUS-C |
| `tm/store` | Fixed-size ring buffer, store-and-forward | — |
| `hal/io` | Platform I/O vtable (read/write + context pointer) | — |

**PUS-C services**

| Module | Description | Standard |
|---|---|---|
| `pus/pus_tm` | Shared PUS-C TM packet builder | ECSS PUS-C |
| `pus/s1` | TC verification: acceptance, start, completion ack/nak | PUS-C S1 |
| `pus/s3` | Housekeeping: static parameter sets, periodic reports | PUS-C S3 |
| `pus/s5` | Event reporting: four severity levels, FDIR coupling | PUS-C S5 |
| `pus/s6` | Memory management: load, check (CRC-16), dump | PUS-C S6 |
| `pus/s8` | Function management: static function table, perform by ID | PUS-C S8 |
| `pus/s17` | Are-you-alive ping/pong | PUS-C S17 |

**FDIR**

| Module | Description |
|---|---|
| `fdir/fsm` | Safe mode FSM: NOMINAL ↔ SAFE, entry/exit hooks, TC whitelist |
| `fdir/watchdog` | Software watchdog: countdown, kick API, expiry callback |
| `hal/arm/trap_table` | ARM Cortex-A exception handler stubs (ZynqMP) |
| `hal/msp430/trap_table` | MSP430 fault handler stubs (safe-mode OBC) |

**SRDB**

| Component | Description |
|---|---|
| `srdb/data/*.yaml` | Mission parameter database: parameters, TCs, HK sets, events |
| `srdb/obsw_srdb/` | Python loader + Pydantic validation + C header codegen |
| `build/include/obsw/srdb_generated.h` | Generated at build time — named constants for C code |

See [docs/architecture.md](docs/architecture.md) for full design documentation.
See [docs/mission-config.md](docs/mission-config.md) for mission-specific parameters.

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

`sim/main.c` reads length-prefixed TC frames from stdin, dispatches them
through the real PUS service handlers, and writes ack records to stdout.

```bash
# Send a S17/1 ping
python3 sim/send_ping.py | ./build/sim/obsw_sim

# Expected output
# stderr: [OBSW] Host sim started (SCID=0x001). Awaiting TC frames on stdin.
# stdout: ACK apid=0x010 svc=17 subsvc=1 flags=0x09 seq=1
```

## Roadmap

| Version | Content | Status |
|---|---|---|
| v0.1 | Space Packet + TC dispatcher + TM store + host sim | ✅ |
| v0.2 | TC Frame (CCSDS 232.0-B) + TM Frame (CCSDS 132.0-B) | ✅ |
| v0.3 | PUS-C S1, S3, S5, S17 | ✅ |
| v0.4 | FDIR: safe mode FSM, watchdog, S5 coupling, trap stubs | ✅ |
| SRDB | YAML parameter database, Python loader, CMake codegen | ✅ |
| v0.5 | S6/S8, MSP430 build profile, Renode, OBCEmulatorAdapter | 🔜 |

## Standards references

| Standard | Title |
|---|---|
| CCSDS 133.0-B | Space Packet Protocol |
| CCSDS 232.0-B | TC Space Data Link Protocol |
| CCSDS 132.0-B | TM Space Data Link Protocol |
| ECSS-E-ST-70-41C | Packet Utilisation Standard (PUS-C) |

## License

Apache 2.0