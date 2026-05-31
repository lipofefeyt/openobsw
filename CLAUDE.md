# CLAUDE.md

@/home/vscode/.claude-global/contexts/openobsw-opensvf.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build commands

Source `scripts/activate.sh` first to get the named aliases. All builds require an active Python venv (`source .venv/bin/activate`) because SRDB code generation runs at CMake build time.

### Host simulation (primary dev target)

```bash
# First-time configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
    -DOBSW_BUILD_TESTS=ON -DOBSW_BUILD_SIM=ON -G Ninja

# Incremental build
cmake --build build -j$(nproc)   # alias: host-build

# Full reconfigure + build
# alias: host-rebuild

# All tests
ctest --test-dir build --output-on-failure   # alias: host-test

# Single test (test names below)
ctest --test-dir build -R <test_name> --output-on-failure

# Run the sim interactively
./build/sim/obsw_sim   # alias: host-sim

# Send a TC(17,1) ping to a running sim
python3 sim/send_ping.py   # alias: host-ping
```

**All ctest test names** (unit: 17, integration: 1):
`adcs`, `bdot`, `dispatcher`, `fsm`, `s1`, `s3`, `s5`, `s5_fdir`, `s6`, `s8`, `s17`, `s20`, `space_packet`, `tc_frame`, `tm_frame`, `tm_store`, `watchdog`, `tc_pipeline`

### aarch64 Linux (QEMU user-mode)

```bash
cmake -S . -B build_aarch64 \
    -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake \
    -DOBSW_BUILD_SIM=ON -DOBSW_BUILD_TESTS=ON -G Ninja
cmake --build build_aarch64   # alias: aarch64-build

# Run under QEMU user-mode
qemu-aarch64 -L $AARCH64_GLIBC ./build_aarch64/sim/obsw_sim   # alias: aarch64-sim
```

### MSP430 bare-metal

```bash
cmake -B build-msp430 \
  -DCMAKE_TOOLCHAIN_FILE=$(pwd)/cmake/msp430-toolchain.cmake \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -S targets/msp430-fr5969
cmake --build build-msp430
```

### STM32H7 bare-metal

```bash
# Configure + build (alias: stm32h7-build)
cmake -S targets/stm32h7 -B build_stm32h7 \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_TOOLCHAIN_FILE=cmake/stm32h7-toolchain.cmake \
    -DOBSW_ROOT=$(pwd)
cmake --build build_stm32h7

# Flash via OpenOCD (alias: stm32h7-flash)
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
    -c "program build_stm32h7/obsw_stm32h7.bin 0x08000000 verify reset exit"

# Size report
arm-none-eabi-size build_stm32h7/obsw_stm32h7.elf   # alias: stm32h7-size

# Build for Renode emulation (skips PLL spin-waits)
cmake -S targets/stm32h7 -B build_stm32h7 ... -DOBSW_RENODE=ON
```

### ZynqMP bare-metal

```bash
cmake -S . -B build_zynqmp_baremetal \
    -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-none-elf.cmake \
    -DOBSW_BUILD_ZYNQMP=ON -DOBSW_BUILD_TESTS=OFF -DOBSW_BUILD_SIM=OFF
cmake --build build_zynqmp_baremetal   # alias: zynqbare-build
```

### Renode emulation

```bash
# ZynqMP
renode renode/zynqmp_obsw.resc          # alias: renode-zynqmp
python3 renode/test_ping_zynqmp.py     # alias: renode-ping-zynqmp

# STM32H7 — build with -DOBSW_RENODE=ON first (skips PLL spin-waits)
renode renode/stm32h750_obsw.resc      # alias: renode-stm32h7
python3 renode/test_ping_stm32h7.py   # alias: renode-ping-stm32h7
```

### SRDB Python tests

```bash
# SRDB loader, codegen, XTCE unit tests
pytest srdb/tests/ -v
```

### AddressSanitizer

```bash
cmake -S . -B build -DOBSW_ENABLE_ASAN=ON -DOBSW_BUILD_TESTS=ON -G Ninja
cmake --build build && ctest --test-dir build --output-on-failure
```

---

## Code style

Formatting is enforced by `.clang-format` (Linux/K&R-adjacent). Key rules:
- 4-space indent, 100-column limit
- Pointer alignment: `uint8_t *buf` (right-aligned)
- `BreakBeforeBraces: Linux` — function braces on new line, control structures attached
- `SortIncludes: CaseInsensitive` with `obsw/` headers grouped before system headers

Apply before committing:

```bash
clang-format -i src/**/*.c include/**/*.h
```

---

## Architecture

### Core design constraints

- **Zero dynamic allocation** — no `malloc` anywhere. All state is statically allocated by the caller and passed in.
- **No global state** — every function takes an explicit context pointer.
- **HAL-isolated** — the core stack never calls platform I/O directly; all I/O goes through `obsw_io_ops_t` (a `read`/`write` vtable). Note: buffer lengths are `uint16_t`, not `size_t`, for portability on 16-bit targets (MSP430).
- **C11, `-Wall -Wextra -Wpedantic`, zero warnings** — enforced in `CMakeLists.txt`.

### Layer model (uplink path)

```
Raw bytes from UART/socket
  → TC Transfer Frame decoder   ccsds/tc_frame.{h,c}   (CRC-16/CCITT FECF)
  → Space Packet parser         ccsds/space_packet.{h,c}
  → TC Dispatcher               tc/dispatcher.{h,c}     (static routing table)
  → PUS service handlers        pus/s{1,3,5,6,8,17,20}.c
  → FDIR FSM gate               fdir/fsm.{h,c}          (SAFE-mode TC whitelist)
```

Downlink mirrors in reverse: PUS handlers → TM store (`tm/store`) → TM frame builder (`ccsds/tm_frame`) → HAL write.

### PUS-C services

| Service | File | Key subservices |
|---|---|---|
| S1 TC Verification | `pus/s1.c` | TM(1,1) accept, TM(1,2) fail, TM(1,7) complete, TM(1,8) fail |
| S3 Housekeeping | `pus/s3.c` | TC(3,1) define set, TC(3,5/6) enable/disable, TM(3,25) report |
| S5 Event Reporting | `pus/s5.c` | TM(5,1–4); HIGH severity events trigger FSM to SAFE |
| S6 Memory Management | `pus/s6.c` | TC(6,2) load, TC(6,5) check CRC, TC(6,9) dump |
| S8 Function Management | `pus/s8.c` | TC(8,1) perform; function ID 1 = recover to NOMINAL |
| S17 Are-You-Alive | `pus/s17.c` | TC(17,1) ping → TM(17,2) pong |
| S20 Parameter Management | `pus/s20.c` | TC(20,1) set, TC(20,3) get, TM(20,2) report |

All TC handlers emit S1 reports. `obsw_crc16_ccitt()` (from `ccsds/tc_frame`) is the single CRC implementation reused by S6 memory check and TM frame FECF.

### Key public types

| Type | Header | Purpose |
|---|---|---|
| `obsw_io_ops_t` | `hal/io.h` | Platform I/O vtable (implement once per target) |
| `obsw_tc_dispatcher_t` | `tc/dispatcher.h` | Routes TC packets to handlers |
| `obsw_tc_route_t` | `tc/dispatcher.h` | Single routing table entry (APID/svc/subsvc → handler); APID `0xFFFF` is wildcard |
| `obsw_fsm_ctx_t` | `fdir/fsm.h` | SAFE ↔ NOMINAL mode state machine |
| `obsw_tm_frame_config_t` | `ccsds/tm_frame.h` | TM frame builder config (SCID, VCID, frame length) |
| `obsw_tm_store_t` | `tm/store.h` | Fixed ring buffer for outgoing TM packets |

TM store capacity is compile-time: `OBSW_TM_STORE_SLOTS` (default 32) and `OBSW_TM_MAX_PACKET_LEN` (default 1024 B). The MSP430 target overrides these to 4 slots × 64 B = 256 B total to fit within 2 KB SRAM.

### Wire protocol v3 (host sim / SVF integration)

Framed pipe protocol over stdin/stdout (host) or Renode socket:

```
→ OBSW  0x01 [uint16 BE len] [TC frame bytes]          TC uplink
→ OBSW  0x02 [uint16 BE len] [obsw_sensor_frame_t]     Sensor injection (sim only)
← OBSW  0x03 [uint16 BE len] [obsw_actuator_frame_t]   Actuator output
← OBSW  0x04 [uint16 BE len] [TM packet bytes]         TM downlink
← OBSW  0xFF                                            End of tick
```

`obsw_sensor_frame_t` (47 B, packed, LE): MAG xyz + valid, ST quaternion + valid, GYRO xyz + valid, sim_time.  
`obsw_actuator_frame_t` (29 B, packed, LE): MTQ dipoles xyz, RW torques xyz, controller byte, sim_time.

The framing code used by ground tools and the SVF harness lives in `contrib/svf_protocol/` — keep it in sync with `sim/main.c` when changing protocol bytes or struct layouts.

### SRDB (Spacecraft Resource Database)

`srdb/` is a Python package (`obsw-srdb`) that reads `srdb/data/*.yaml` and generates:
- `build/include/obsw/srdb_generated.h` — C header with parameter IDs, types, limits
- `build/xtce/mission.xtce` — XTCE 1.2 export for YAMCS/OpenSVF

Codegen runs automatically at CMake build time. The YAML files are the authoritative source of truth:

| File | Contents |
|---|---|
| `srdb/data/spacecraft.yaml` | SCID (0x001), default APID (0x010), mission identity |
| `srdb/data/parameters.yaml` | TM parameter definitions — ID, type, limits |
| `srdb/data/telecommands.yaml` | TC definitions |
| `srdb/data/events.yaml` | S5 event IDs |
| `srdb/data/hk_sets.yaml` | S3 housekeeping set definitions |
| `srdb/data/hardware/` | Equipment hardware profiles for OpenSVF |

### AOCS

Two-mode control law, called each tick from `sim/main.c` (host) and `src/platform/*/main.c` (bare-metal):

| FSM mode | Algorithm | Sensors | Actuators |
|---|---|---|---|
| SAFE | B-dot (`aocs/bdot.c`) | Magnetometer only | Magnetorquers |
| NOMINAL | PD quaternion (`aocs/adcs.c`) | Star tracker + gyro | Reaction wheels |

### Tests

17 C unit tests + 1 integration test (`tc_pipeline`), all using the Unity framework (fetched via CMake `FetchContent`). Test files are in `test/unit/` and `test/integration/`. ctest names match the module without the `test_` prefix (e.g., `test_s17.c` → `-R s17`).

### Mission-specific configuration

Before flight use, set in code: SCID (`0x001` in sim), VCID (`0x00`), TM frame data field length (`64` bytes in sim), APID allocation (TC nominal: `0x010`). See `docs/mission-config.md` for the full checklist.

---

## Further reading

`docs/` contains deep-dive guides that go beyond this file:

| File | Contents |
|---|---|
| `docs/architecture.md` | Full module documentation, data-flow diagrams, AOCS algorithm derivation |
| `docs/integration-guide.md` | OpenSVF closed-loop integration, wire protocol details, SVF Track B handoff |
| `docs/mission-config.md` | Pre-flight configuration checklist (SCID, APID, frame lengths, CRC seeds) |
| `docs/msp430-build.md` | MSP430 hardware build, flash, and debug with LaunchPad |
