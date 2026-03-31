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
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Requirements: `gcc`, `cmake >= 3.16`, `git` (Unity fetched automatically).

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
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
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
| `pus/pus_tm` | Shared PUS-C TM packet builder — used by all services | ECSS PUS-C |
| `pus/s1` | TC verification: acceptance, start, completion ack/nak | PUS-C S1 |
| `pus/s3` | Housekeeping: static parameter sets, periodic reports, enable/disable | PUS-C S3 |
| `pus/s5` | Event reporting: four severity levels, FDIR safe-trigger coupling | PUS-C S5 |
| `pus/s17` | Are-you-alive ping/pong | PUS-C S17 |

**FDIR**

| Module | Description |
|---|---|
| `fdir/fsm` | Safe mode FSM: NOMINAL ↔ SAFE, entry/exit hooks, TC whitelist |
| `fdir/watchdog` | Software watchdog: countdown, kick API, expiry callback |
| `hal/arm/trap_table` | ARM Cortex-A exception handler stubs (ZynqMP) |
| `hal/msp430/trap_table` | MSP430 fault handler stubs (safe-mode OBC) |

See [docs/architecture.md](docs/architecture.md) for full design documentation.

## Test coverage

```
13 suites / 86 tests / 0 failures / 0 warnings
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
| test_s17 | 5 | unit |
| test_fsm | 11 | unit |
| test_watchdog | 9 | unit |
| test_s5_fdir | 5 | unit |
| test_tc_pipeline | 2 | integration |

## Host simulation

`sim/main.c` is a host-mode process that reads length-prefixed TC frames
from stdin and writes TM ack records to stdout. It is the future integration
seam for the OpenSVF `OBCEmulatorAdapter`.

```bash
# Send a S17/1 ping manually
python3 sim/send_ping.py | ./build/sim/obsw_sim
```

## Roadmap

| Version | Content | Status |
|---|---|---|
| v0.1 | Space Packet + TC dispatcher + TM store + host sim | ✅ |
| v0.2 | TC Frame (CCSDS 232.0-B) + TM Frame (CCSDS 132.0-B) | ✅ |
| v0.3 | PUS-C service handlers: S1, S3, S5, S17 | ✅ |
| v0.4 | FDIR: safe mode FSM, watchdog, S5 FDIR coupling, trap stubs | ✅ |
| v0.5 | Renode sentinel peripheral + OpenSVF OBCEmulatorAdapter | 🔜 |

## Standards references

| Standard | Title |
|---|---|
| CCSDS 133.0-B | Space Packet Protocol |
| CCSDS 232.0-B | TC Space Data Link Protocol |
| CCSDS 132.0-B | TM Space Data Link Protocol |
| ECSS-E-ST-70-41C | Packet Utilisation Standard (PUS-C) |

## License

Apache 2.0
