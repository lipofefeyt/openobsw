# openobsw

Open-source on-board software core for satellite applications.

A portable, zero-allocation TT&C middleware stack in C11, designed to run on
bare metal, FreeRTOS, or RTEMS — and to integrate directly with
[OpenSVF](https://github.com/lipofefeyt/opensvf) for system-level validation.

## Design principles

- **Zero dynamic allocation** — all state is caller-owned; no heap use inside the library
- **No global state** — context pointers carried throughout; safe for multi-instance use
- **Portable** — C11, no compiler extensions, no POSIX dependencies in the core library
- **Validation-first** — every module ships with a unit test suite from day one
- **HAL-isolated** — platform I/O is a vtable; the core stack never calls hardware directly

## Repository structure

```
openobsw/
├── include/obsw/
│   ├── obsw.h              # Single top-level include
│   ├── ccsds/
│   │   ├── space_packet.h  # CCSDS 133.0-B Space Packet encode/decode
│   │   ├── tc_frame.h      # CCSDS 232.0-B TC frame (v0.2)
│   │   └── tm_frame.h      # CCSDS 132.0-B TM frame (v0.2)
│   ├── tc/
│   │   └── dispatcher.h    # TC command router, zero-alloc static table
│   ├── tm/
│   │   └── store.h         # TM ring buffer (store-and-forward)
│   └── hal/
│       └── io.h            # Thin I/O vtable abstraction
├── src/                    # Library implementation
├── sim/                    # Host simulation harness (stdin/stdout)
├── test/unit/              # Unity-based unit tests
└── docs/                   # Architecture and standard references
```

## What's in v0.1

| Module | File | Description | Standard |
|---|---|---|---|
| Space Packet | `ccsds/space_packet` | Primary header encode/decode, full packet parse | CCSDS 133.0-B |
| TC Dispatcher | `tc/dispatcher` | Command router, static table, handler + responder API | ECSS PUS-C |
| TM Store | `tm/store` | Fixed-size ring buffer, store-and-forward | — |
| HAL I/O | `hal/io` | Platform I/O vtable (read/write + context pointer) | — |

## Architecture

```
TC frame (raw bytes)
        |
        v
obsw_tc_dispatcher_feed()
        |
        +-- obsw_sp_parse()              space_packet.c
        |       |
        |       +-- decode primary header (APID, svc, subsvc, seq)
        |
        +-- route table lookup (APID / service / subservice)
        |
        +-- handler(tc, responder, ctx)   application code
                |
                v
         responder(flags, tc, ctx)        emit TM ack/nak
```

### TC Dispatcher routing table

The routing table is a static array of `obsw_tc_route_t` entries, owned by
the caller and passed in at `obsw_tc_dispatcher_init`. No heap, no dynamic
registration. First match wins. APID `0xFFFF` is a wildcard.

```c
static obsw_tc_route_t routes[] = {
    { .apid = 0xFFFF, .service = 17, .subservice = 1,
      .handler = handle_s17_ping, .ctx = NULL },
};
```

### HAL I/O abstraction

The core stack never calls platform I/O directly. All I/O goes through
`obsw_io_ops_t`, a vtable with `read` and `write` function pointers.
Platform ports implement this once:

| Target | Implementation |
|---|---|
| Host sim | stdin / stdout |
| Renode | Memory-mapped sentinel peripheral |
| Bare metal | UART / SpaceWire driver |

## Building

Requirements: `gcc`, `cmake >= 3.16`, `git` (for Unity fetch).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

With AddressSanitizer:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DOBSW_ENABLE_ASAN=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### Recommended shell aliases

```bash
alias omkdebug='cmake -B build -DCMAKE_BUILD_TYPE=Debug'
alias omkbuild='cmake --build build'
alias otest='ctest --test-dir build --output-on-failure'
alias oclean='rm -rf build'
```

### Clean rebuild

CMake caches build state. When `CMakeLists.txt` files change, always do a
clean rebuild:

```bash
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Test coverage (v0.1)

| Suite | Tests | What's covered |
|---|---|---|
| `test_space_packet` | 8 | Encode/decode round-trip, null guards, max APID, max seq count, full parse, truncated frame, total length |
| `test_dispatcher` | 6 | Null init, successful dispatch, no-route NACK, wildcard APID, handler error, first-match-wins |
| `test_tm_store` | 6 | Init, enqueue/dequeue, FIFO order, empty dequeue, oversized packet, full store |

## Host simulation harness

`sim/main.c` is a host-mode process that reads length-prefixed TC frames
from stdin and writes TM ack records to stdout. It is the integration seam
for the future `OBCEmulatorAdapter` in OpenSVF.

Frame format on stdin: `[uint16 BE length][frame bytes]`

```bash
./build/sim/obsw_sim
```

## Roadmap

| Version | Content |
|---|---|
| v0.1 | Space Packet + TC dispatcher + TM store + host sim ✅ |
| v0.2 | TC Frame parser (CCSDS 232.0-B) + TM Frame builder (CCSDS 132.0-B) |
| v0.3 | PUS-C service handlers: S1 (TC verification), S3 (housekeeping), S5 (event), S17 (ping) |
| v0.4 | FDIR framework: watchdog, safe mode FSM, trap/exception integration |
| v0.5 | Renode sentinel peripheral + OpenSVF OBCEmulatorAdapter integration |

## Standards references

| Standard | Title |
|---|---|
| CCSDS 133.0-B | Space Packet Protocol |
| CCSDS 232.0-B | TC Space Data Link Protocol |
| CCSDS 132.0-B | TM Space Data Link Protocol |
| CCSDS 131.0-B | TM Synchronization and Channel Coding |
| ECSS-E-ST-70-41C | Packet Utilisation Standard (PUS-C) |

## Relationship to OpenSVF

openobsw is the reference OBSW target for
[OpenSVF](https://github.com/lipofefeyt/opensvf). The integration path is:

```
openobsw binary
      |
      | runs on
      v
Renode emulator  ←——  OBCEmulatorAdapter (OpenSVF M10)
      |
      | DDS lockstep
      v
OpenSVF SimulationMaster
      |
      | drives
      v
Equipment models (RW, ST, PCDU, S-Band)
```

This completes the ECSS-E-TM-10-21A system validation level — the OBSW
interacting with validated equipment models in closed loop.

## License

Apache 2.0