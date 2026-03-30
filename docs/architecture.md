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
 │                   Application / PUS handlers             │
 │         (S1 verification, S3 HK, S5 events, S17 ping)   │
 └────────────────────────┬────────────────────────────────┘
                          │  obsw_tc_t (parsed command)
 ┌────────────────────────▼────────────────────────────────┐
 │              TC Dispatcher  (tc/dispatcher.h)            │
 │   static routing table: APID / service / subservice      │
 │   → handler(tc, responder, ctx)                          │
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
| spacecraft_id | 10 | SCID |
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

```
Uplink (ground → satellite)
────────────────────────────
[CLTU bytes]
    │
    │  HAL io.read()
    ▼
[TC Transfer Frame]
    │
    │  obsw_tc_frame_decode()   — parse header, validate CRC
    ▼
[TC Frame data field = Space Packet bytes]
    │
    │  obsw_tc_dispatcher_feed()
    │    └─ obsw_sp_parse()     — decode primary header
    │    └─ PUS sec hdr extract — service / subservice
    │    └─ route table lookup
    ▼
[handler(tc, responder, ctx)]
    │
    │  responder(ACCEPT | COMPLETE, tc, ctx)
    ▼
[TM ack space packet → obsw_tm_store_enqueue()]


Downlink (satellite → ground)
─────────────────────────────
[obsw_tm_store_dequeue()]
    │
    │  obsw_tm_frame_build()    — pack packets, idle fill, append CRC
    ▼
[TM Transfer Frame bytes]
    │
    │  HAL io.write()
    ▼
[downlink channel]
```

---

## OpenSVF integration (v0.5)

openobsw is the reference OBSW target for
[OpenSVF](https://github.com/lipofefeyt/opensvf).
The integration path:

```
openobsw binary
      │  runs on
      ▼
Renode emulator  ◄──  Sentinel peripheral (memory-mapped, DDS lockstep)
      │
      │  OBCEmulatorAdapter (implements ModelAdapter)
      ▼
OpenSVF SimulationMaster
      │  drives
      ▼
Equipment models (Reaction Wheel, Star Tracker, PCDU, S-Band)
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