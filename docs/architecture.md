# openobsw — Architecture

## Overview

openobsw is a C11 TT&C middleware stack for satellite on-board software.
It implements the uplink (TC) and downlink (TM) data paths defined by the
CCSDS and ECSS PUS-C standards, from raw frame bytes down to application
command handlers — with no dynamic allocation, no global state, and a clean
HAL boundary between the protocol stack and platform I/O.

It is designed to be the reference OBSW target for
[OpenSVF](https://github.com/lipofefeyt/opensvf), enabling closed-loop
system validation against equipment models.

---

## Design principles

**Zero dynamic allocation.** All state is caller-owned. No `malloc`, no heap
use anywhere inside the library. Routing tables, ring buffers and dispatcher
contexts are statically allocated by the application and passed in at init.

**No global state.** Every function takes an explicit context pointer.
Multiple dispatcher instances can coexist safely; there is no shared mutable
state between them.

**Portable C11.** No compiler extensions, no POSIX dependencies in the core
library. Compiles cleanly under `-Wall -Wextra -Wpedantic` with zero warnings.

**HAL-isolated.** The core stack never calls platform I/O directly.
All I/O goes through `obsw_io_ops_t`, a vtable of function pointers.
Platform ports implement this once; the rest of the stack is untouched.

**Validation-first.** Every module ships with a unit test suite.
An integration test exercises the full uplink/downlink path end-to-end.

---

## Layer model

```
 ┌─────────────────────────────────────────────────────────┐
 │                   FDIR                                   │
 │    FSM (safe mode) · Watchdog · Trap handlers            │
 └────────────────────────┬────────────────────────────────┘
                          │  mode transitions / safe triggers
 ┌────────────────────────▼────────────────────────────────┐
 │              PUS-C Services                              │
 │    S1 · S3 · S5 · S6 (memory) · S8 (functions) · S17   │
 └────────────────────────┬────────────────────────────────┘
                          │  obsw_tc_t (parsed command)
 ┌────────────────────────▼────────────────────────────────┐
 │              TC Dispatcher  (tc/dispatcher.h)            │
 │   static routing table: APID / service / subservice      │
 │   → obsw_fsm_tc_allowed() check in SAFE mode             │
 └────────────────────────┬────────────────────────────────┘
                          │  raw space packet bytes
 ┌────────────────────────▼────────────────────────────────┐
 │           Space Packet layer  (ccsds/space_packet.h)     │
 │   primary header encode / decode / parse                 │
 └────────────────────────┬────────────────────────────────┘
                          │  TC frame data field
 ┌────────────────────────▼────────────────────────────────┐
 │           TC Frame layer  (ccsds/tc_frame.h)             │
 │   primary header decode, CRC-16/CCITT validation         │
 └────────────────────────┬────────────────────────────────┘
                          │  raw bytes from uplink channel
 ┌────────────────────────▼────────────────────────────────┐
 │               HAL I/O  (hal/io.h)                        │
 │   obsw_io_ops_t vtable: read() / write() / ctx           │
 └─────────────────────────────────────────────────────────┘
```

The downlink path mirrors this in reverse:

```
 Application / PUS handler
         │  enqueue TM packet
         ▼
 TM Store  (tm/store.h)
         │  dequeue TM packet
         ▼
 TM Frame builder  (ccsds/tm_frame.h)
         │  raw frame bytes
         ▼
 HAL I/O write()
```

---

## Module reference

### `ccsds/space_packet` — CCSDS 133.0-B

The fundamental unit of the CCSDS stack. Every TC command and TM telemetry
packet is a space packet.

Key types: `obsw_sp_primary_hdr_t`, `obsw_sp_packet_t`

Key functions:
- `obsw_sp_encode_primary()` — serialise primary header to 6 bytes
- `obsw_sp_decode_primary()` — deserialise 6 bytes into header struct
- `obsw_sp_parse()` — decode header + set payload pointer (zero-copy)

---

### `ccsds/tc_frame` — CCSDS 232.0-B

Decodes TC Transfer Frames received from the uplink channel.
Validates the CRC-16/CCITT FECF before exposing the data field.

Key types: `obsw_tc_frame_header_t`, `obsw_tc_frame_t`

Key functions:
- `obsw_tc_frame_decode()` — parse header, validate CRC, set data pointer
- `obsw_crc16_ccitt()` — poly 0x1021, init 0xFFFF; also used by TM frame

Primary header fields decoded:

| Field | Bits | Notes |
|---|---|---|
| version | 2 | Always 0b00 |
| bypass_flag | 1 | Type-A/B service |
| ctrl_cmd_flag | 1 | Control command |
| spacecraft_id | 10 | SCID — 10-bit value, unique per spacecraft, assigned by mission authority. Must match across TC frame decoder and TM frame builder. Mission-dependent — see [docs/mission-config.md](mission-config.md). |
| virtual_channel_id | 6 | VCID |
| frame_len | 10 | Total frame octets - 1 |
| frame_seq_num | 8 | Wraps at 255 |

---

### `ccsds/tm_frame` — CCSDS 132.0-B

Builds TM Transfer Frames for downlink. Packs TM space packets into the
data field zone; fills unused space with idle packets per CCSDS 133.0-B §4.3.
Optionally appends CRC-16/CCITT FECF.

Key types: `obsw_tm_frame_config_t`

Key functions:
- `obsw_tm_frame_build()` — encode header, embed payload, fill idle, append CRC

Configuration is per virtual channel and typically constant for a mission:

```c
obsw_tm_frame_config_t cfg = {
    .spacecraft_id               = 0x001,
    .virtual_channel_id          = 0,
    .master_channel_frame_count  = 0,   /* incremented by caller each frame */
    .virtual_channel_frame_count = 0,
    .enable_fecf                 = 1,
    .frame_data_field_len        = 64,  /* mission-specific */
};
```

---

### `pus/s6` — PUS-C S6 Memory Management

Provides ground-commanded memory load, check and dump. All operations
work on raw memory addresses encoded as 4-byte BE fields (PUS-C standard,
correct for 32-bit targets).

Key handlers: `obsw_s6_load()`, `obsw_s6_check()`, `obsw_s6_dump()`

| Subservice | TC | TM response |
|---|---|---|
| 2 | Load memory area | TM(1,1) + TM(1,7) |
| 5 | Check memory area (CRC-16/CCITT) | TM(6,10) match / TM(6,11) mismatch |
| 9 | Dump memory area | TM(6,6) data packet |

Reuses `obsw_crc16_ccitt()` from the TC frame layer — single CRC
implementation across the whole stack.

---

### `pus/s8` — PUS-C S8 Function Management

Executes named on-board functions by ID from a static caller-owned table.
Replaces the earlier mission-defined `TC(128,1)` recovery command with a
standards-compliant interface.

Key handler: `obsw_s8_perform()`

| Function ID | Name | Effect |
|---|---|---|
| 1 | `OBSW_S8_FN_RECOVER_NOMINAL` | Calls `obsw_fsm_to_nominal()` |

Additional function IDs are registered by the application in `obsw_s8_entry_t[]`.

---

### `tc/dispatcher` — ECSS PUS-C

Routes incoming TC space packets to registered handlers via a static lookup
table. First match wins. APID `0xFFFF` is a wildcard that matches any APID.

```c
static obsw_tc_route_t routes[] = {
    { .apid = 0xFFFF, .service = 17, .subservice = 1,
      .handler = handle_s17_ping, .ctx = NULL },
    { .apid = 0x010,  .service =  3, .subservice = 5,
      .handler = handle_s3_enable_hk, .ctx = &hk_ctx },
};

obsw_tc_dispatcher_init(&dispatcher, routes,
                         sizeof(routes)/sizeof(routes[0]),
                         my_responder, NULL);
```

Handlers receive a parsed `obsw_tc_t` and a `responder` callback:

```c
int handle_s17_ping(const obsw_tc_t *tc,
                     obsw_tc_responder_t respond,
                     void *ctx)
{
    respond(OBSW_TC_ACK_ACCEPT | OBSW_TC_ACK_COMPLETE, tc, ctx);
    return 0;
}
```

If no route matches, the responder is called with `OBSW_TC_ACK_REJECT`.

---

### `tm/store` — TM ring buffer

Fixed-size power-of-two ring buffer for outgoing TM packets.
Telemetry sources enqueue; the TM frame builder dequeues.
Size configured at compile time via `OBSW_TM_STORE_SLOTS` (default 32)
and `OBSW_TM_MAX_PACKET_LEN` (default 1024 bytes).

---

### `hal/io` — Platform I/O vtable

```c
typedef struct {
    int (*write)(const uint8_t *buf, size_t len, void *ctx);
    int (*read) (uint8_t *buf, size_t len, void *ctx);
    void *ctx;
} obsw_io_ops_t;
```

| Platform | Implementation |
|---|---|
| Host sim (current) | stdin / stdout |
| Renode (v0.5) | Memory-mapped sentinel peripheral |
| Bare metal | UART / SpaceWire driver |

---

## Full data flow

```mermaid
flowchart TD
    subgraph UPLINK ["Uplink  (ground → satellite)"]
        A([CLTU / raw bytes]) --> B["HAL io.read()"]
        B --> C["TC Transfer Frame\nobsw_tc_frame_decode()\nparse header · validate CRC-16"]
        C --> D["Space Packet\nobsw_sp_parse()\ndecode primary header"]
        D --> E["TC Dispatcher\nobsw_tc_dispatcher_feed()\nextract PUS svc/subsvc · route lookup"]
        E --> F["Handler\nhandler(tc, responder, ctx)"]
        F --> G["responder()\nACCEPT | COMPLETE or REJECT"]
    end

    subgraph DOWNLINK ["Downlink  (satellite → ground)"]
        G --> H["TM Store\nobsw_tm_store_enqueue()"]
        H --> I["TM Store\nobsw_tm_store_dequeue()"]
        I --> J["TM Transfer Frame\nobsw_tm_frame_build()\npack packets · idle fill · FECF"]
        J --> K["HAL io.write()"]
        K --> L([downlink channel])
    end
```

---

## FDIR

### Safe mode FSM — `fdir/fsm.h`

Two-state mode machine. All state is caller-owned; no globals.

```mermaid
stateDiagram-v2
    [*] --> NOMINAL : obsw_fsm_init()

    NOMINAL --> SAFE : obsw_fsm_to_safe()\n(watchdog expiry / trap handler\n/ S5 HIGH safe-trigger event)

    SAFE --> NOMINAL : obsw_fsm_recover() TC handler\n(ground-commanded only)
```

**Entry/exit hooks** are mission-defined function pointers registered in
`obsw_fsm_config_t`. On entry to SAFE: disable payload, switch to safe beacon.
On exit: re-enable nominal operations. Neither hook is mandatory.

**TC whitelist** controls which commands are accepted in SAFE mode. All
others are rejected with `TM(1,2)`. The dispatcher calls
`obsw_fsm_tc_allowed()` before routing. A typical safe-mode whitelist:

```c
static const obsw_fsm_tc_entry_t safe_whitelist[] = {
    { .service = 17, .subservice = 1 },  /* S17 ping      */
    { .service =  1, .subservice = 1 },  /* S1 acceptance */
    { .service = 128, .subservice = 1 }, /* mission: recover command */
};
```

**Recovery** is ground-commanded only. `obsw_fsm_recover()` is a TC handler
the application registers at a mission-defined APID/svc/subsvc. It emits
`TM(1,1)` and `TM(1,7)` via S1 and transitions the FSM back to NOMINAL.

---

### Watchdog — `fdir/watchdog.h`

Software countdown watchdog. Zero dependencies on OS timers.

```
Control cycle:
  obsw_wd_kick(&wd)    ← called by monitored task
  obsw_wd_tick(&wd)    ← called once per cycle after all kicks

  If kicked:  countdown reloaded to timeout_ticks
  If not:     countdown decrements
  At zero:    expire_cb() fired once (latched — no repeat)
```

Typical wiring to the FSM:

```c
static void on_wd_expiry(void *ctx)
{
    obsw_s5_report(&s5, OBSW_S5_HIGH, EVENT_WD_EXPIRY, NULL, 0);
    obsw_fsm_to_safe((obsw_fsm_ctx_t *)ctx);
}

obsw_wd_init(&wd, 10, on_wd_expiry, &fsm);
```

---

### S5 safe-trigger coupling

`obsw_s5_ctx_t` carries an optional FSM pointer and a static list of
`safe_trigger_ids`. If an `OBSW_S5_HIGH` report is emitted with a matching
event ID, `obsw_fsm_to_safe()` is called automatically:

```c
s5.fsm                 = (struct obsw_fsm_ctx *)&fsm;
s5.safe_trigger_ids[0] = EVENT_POWER_FAULT;
s5.safe_trigger_ids[1] = EVENT_COMMS_LOSS;
s5.safe_trigger_count  = 2;
```

If `fsm` is NULL, S5 behaves exactly as in v0.3 — no breaking change.

---

### FDIR event flow

```mermaid
flowchart TD
    WD[Watchdog expiry\nobsw_wd_tick()]
    TRAP[Trap handler\ndata abort / prefetch / stack overflow]
    S5[S5 HIGH event\nmatching safe_trigger_id]

    WD   --> SAFE[obsw_fsm_to_safe()]
    TRAP --> SAFE
    S5   --> SAFE

    SAFE --> HOOK[on_enter_safe hook\ndisable payload\nswitch beacon]
    SAFE --> TM[TM store\nS5 event report enqueued]

    RECOVER[Ground TC\nobsw_fsm_recover()] --> NOM[NOMINAL\non_exit_safe hook]
```

---

### Trap table stubs

Platform-specific fault handlers live in `src/hal/arm/` and
`src/hal/msp430/`. They are declared `__attribute__((weak))` — the
application overrides them with strong definitions.

Each handler must:
1. Emit an `OBSW_S5_HIGH` event (if S5 context is available).
2. Call `obsw_fsm_to_safe()`.
3. Halt (`while(1)`) or trigger a hardware reset — mission policy.

| Platform | File | Vectors covered |
|---|---|---|
| ARM Cortex-A (ZynqMP) | `src/hal/arm/trap_table.c` | Undefined instruction, data abort, prefetch abort, stack overflow |
| MSP430 | `src/hal/msp430/trap_table.c` | WDT expiry, vacant vector, system NMI |

---

## OpenSVF integration (v0.5)

openobsw is the reference OBSW target for
[OpenSVF](https://github.com/lipofefeyt/opensvf).
The integration path:

```mermaid
flowchart TD
    A([openobsw binary]) --> B[Renode emulator]
    B <-->|DDS lockstep| C[Sentinel peripheral\nmemory-mapped]
    C --> D[OBCEmulatorAdapter\nimplements ModelAdapter]
    D --> E[OpenSVF SimulationMaster]
    E --> F[Reaction Wheel model]
    E --> G[Star Tracker model]
    E --> H[PCDU model]
    E --> I[S-Band Transponder model]
```

The three deferred OpenSVF requirements that gate this integration are:
- `SVF-DEV-029` — CCSDS adapter
- `SVF-DEV-034` — ParameterStoreDdsBridge
- `SVF-DEV-037` — PUS command adapter

---

## Standards references

| Standard | Title |
|---|---|
| CCSDS 133.0-B | Space Packet Protocol |
| CCSDS 232.0-B | TC Space Data Link Protocol |
| CCSDS 132.0-B | TM Space Data Link Protocol |
| CCSDS 131.0-B | TM Synchronization and Channel Coding |
| ECSS-E-ST-70-41C | Packet Utilisation Standard (PUS-C) |

---

## MSP430FR5969 target

### Overview

The MSP430FR5969 is the reference hardware target for openobsw. It serves
as the **safe-mode OBC** in a dual-OBC topology — small enough to validate
the complete stack on real hardware, constrained enough to prove the
zero-allocation design holds under genuine memory pressure.

| Resource | Value |
|---|---|
| Architecture | 16-bit RISC, MSP430 |
| SRAM | 2 KB (0x2000–0x27FF) |
| FRAM | 47 KB (0x4400–0xFF7F) |
| Toolchain | TI msp430-elf-gcc 9.3.1.11 |

### MSP430 build profile

The standard build uses default sizing macros. The MSP430 profile overrides
them at compile time to fit within 2KB SRAM:

| Parameter | Default | MSP430 profile |
|---|---|---|
| `OBSW_TM_STORE_SLOTS` | 32 | 4 |
| `OBSW_TM_MAX_PACKET_LEN` | 1024 B | 64 B |
| `OBSW_TC_FRAME_MAX_LEN` | 1024 B | 128 B |
| `OBSW_S5_MAX_SAFE_TRIGGERS` | 8 | 4 |

TM store total: 4 × 64 = **256 bytes**.

### HAL UART (USCI_A1)

`src/hal/msp430/uart.c` implements `obsw_io_ops_t` using USCI_A1 on
P2.0/P2.1 — the backchannel UART on the FR5969 LaunchPad. The same
interface is used by Renode's emulated UART peripheral, so no code changes
are needed when switching between emulation and hardware.

Note: `obsw_io_ops_t` uses `uint16_t` for buffer lengths (not `size_t`)
for portability across 16-bit targets where `size_t` is `__int20 unsigned`.

### Build outputs

```
build-msp430/
  openobsw-msp430.elf   — ELF image for Renode and GDB debugging
  openobsw-msp430.hex   — Intel HEX for mspdebug / UniFlash flashing
  openobsw-msp430.map   — linker symbol map
```

### Cross-compilation

```bash
cmake -B build-msp430 \
  -DCMAKE_TOOLCHAIN_FILE=$(pwd)/cmake/msp430-toolchain.cmake \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -S targets/msp430-fr5969
cmake --build build-msp430
```

See [msp430-build.md](msp430-build.md) for toolchain installation.

### VS Code IntelliSense

The MSP430 build generates `build-msp430/compile_commands.json`. Point
`c_cpp_properties.json` at it so IntelliSense resolves `srdb_generated.h`
and MSP430 register names correctly. See the README for the configuration.

---

## AOCS — Attitude and Orbit Control

### Overview

openobsw implements a two-mode AOCS control law that runs as part of the
main control cycle in `sim/main.c` (host sim) and will run as a dedicated
task in the FreeRTOS target (v0.7).

```
FSM mode       Sensors used          Algorithm       Actuators
─────────────────────────────────────────────────────────────────
SAFE           Magnetometer          B-dot            Magnetorquers
NOMINAL        Star tracker + Gyro   PD quaternion    Reaction wheels
```

### B-dot controller (`aocs/bdot.c`)

Classical detumbling algorithm for safe mode. Requires only a magnetometer
— appropriate when the spacecraft may be tumbling and the star tracker is
unavailable.

```
m_cmd = -k * dB/dt

where:
  m_cmd  = magnetorquer dipole command [A·m²]
  k      = controller gain [A·m²·s/T]
  dB/dt  ≈ (B_now - B_prev) / dt   (finite difference)
```

Key properties:
- Zero-allocation, no trigonometry, suitable for MSP430
- First step always outputs zero (no derivative available)
- Dipole commands saturated at configurable `max_dipole`
- `obsw_bdot_reset()` clears state on mode re-entry

### PD attitude controller (`aocs/adcs.c`)

Quaternion-based PD controller for nominal mode. Uses multiplicative
quaternion error to avoid gimbal lock and singularities.

```
q_err = q_cmd ⊗ q_meas*           (multiplicative error)
τ_cmd = -Kp * q_err_vec - Kd * ω  (PD law)

where:
  q_err_vec = vector part of q_err (x, y, z components)
  ω         = angular velocity from gyro [rad/s]
```

Key properties:
- Short-path enforcement: flips q_err sign if w < 0
- Torque saturated at configurable `max_torque`
- Quaternion utilities: normalise, conjugate, multiply
- Returns `false` if measured quaternion has near-zero norm

### Sensor injection protocol (simulation only)

The host sim extends the pipe protocol with a type-prefixed framing layer.
This is **simulation-only** — it does not exist in flight software.

```
Type 0x01 — TC uplink (unchanged):
  [0x01][uint16 BE length][TC frame bytes]

Type 0x02 — Sensor injection:
  [0x02][uint16 BE length][obsw_sensor_frame_t bytes]
```

`obsw_sensor_frame_t` carries magnetometer (Tesla), star tracker
(quaternion), and gyro (rad/s) data with per-sensor validity flags.
This matches OpenSVF equipment model output units directly.

The `OBCEmulatorAdapter` in OpenSVF reads sensor values from
`ParameterStore` each tick and packages them as type-0x02 frames
before sending the heartbeat TC. Sensor frames are parsed in
`sim/sensor_inject.c` and fed directly to the AOCS algorithms.

### AOCS telemetry

The control loop emits observability TM each cycle:

| Packet | Contents |
|---|---|
| TM(8,128) | B-dot report: `dB/dt[3]`, `m_cmd[3]` |
| TM(8,129) | PD report: `torque_cmd[3]`, `angle_err_rad`, `q_err[4]` |

These are parsed by `OBCEmulatorAdapter._parse_tm()` and written to
`ParameterStore` so OpenSVF scenarios can assert on AOCS behaviour.