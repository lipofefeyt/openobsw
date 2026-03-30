# openobsw — Mission Configuration

openobsw is a mission-agnostic library. Several parameters are spacecraft-
or mission-specific and must be defined by the integrating project before
flight use. This document lists all such parameters, their roles, and the
values used in the openobsw test harness and host simulation.

---

## Mission-specific parameters

### Spacecraft ID (SCID)

| Property | Value |
|---|---|
| Field width | 10 bits |
| Range | 0x000 – 0x3FF |
| Scope | TC Transfer Frame header (CCSDS 232.0-B) and TM Transfer Frame header (CCSDS 132.0-B) |
| Assigned by | Mission authority (e.g. CCSDS registry or mission operations team) |

The SCID identifies the spacecraft uniquely on the link. It must be
consistent across all uplink and downlink processing:
- The TC frame decoder validates incoming frames against the expected SCID.
- The TM frame builder embeds the SCID in every outgoing frame.

There is one SCID per physical spacecraft. It does not change during the
mission lifetime.

**openobsw test/sim value:** `0x001` (placeholder — not a registered SCID)

---

### Virtual Channel ID (VCID)

| Property | Value |
|---|---|
| Field width | 6 bits (TC), 3 bits (TM) |
| Range | 0x00 – 0x3F (TC) / 0x0 – 0x7 (TM) |
| Scope | TC and TM Transfer Frame headers |

Virtual channels multiplex different traffic streams over a single physical
link. Common assignments:

| VCID | Typical use |
|---|---|
| 0 | Nominal TC / TM |
| 1 | Real-time housekeeping |
| 2 | Stored data downlink |
| 7 | Idle frames |

Mission VCIDs must be agreed with the ground segment.

**openobsw test/sim value:** `0x00`

---

### TM Frame Data Field Length

| Property | Value |
|---|---|
| Type | `uint16_t` in `obsw_tm_frame_config_t` |
| Typical range | 64 – 1105 bytes |
| Constraints | Fixed per virtual channel; agreed with ground segment |

Defines the fixed size of the TM frame data zone. Shorter than the maximum
frame reduces downlink efficiency; longer increases latency between packet
boundaries. Set once per mission and virtual channel.

**openobsw test/sim value:** `64` (placeholder)

---

### TC Frame Maximum Length

| Property | Value |
|---|---|
| Macro | `OBSW_TC_FRAME_MAX_LEN` in `tc_frame.h` |
| Default | `1024` bytes (CCSDS maximum) |

Can be reduced for memory-constrained targets. Override at compile time:

```cmake
target_compile_definitions(obsw PRIVATE OBSW_TC_FRAME_MAX_LEN=256)
```

---

### TM Store Sizing

| Parameter | Macro | Default |
|---|---|---|
| Number of slots | `OBSW_TM_STORE_SLOTS` | 32 |
| Max packet size | `OBSW_TM_MAX_PACKET_LEN` | 1024 bytes |

`OBSW_TM_STORE_SLOTS` must be a power of two. Size according to the worst-
case TM burst between frame builder calls. Override at compile time:

```cmake
target_compile_definitions(obsw PRIVATE
    OBSW_TM_STORE_SLOTS=64
    OBSW_TM_MAX_PACKET_LEN=512
)
```

---

### Application Process IDs (APIDs)

| Property | Value |
|---|---|
| Field width | 11 bits |
| Range | 0x000 – 0x7FE (0x7FF reserved for idle packets) |
| Assigned by | Mission authority |

APIDs identify the application process that originates or consumes a space
packet. In the TC dispatcher routing table, each handler is registered
against a specific APID (or `0xFFFF` as a wildcard).

APID allocation should be documented in the mission's Packet Utilisation
Standard (PUS) implementation document.

**openobsw test/sim APIDs:**

| APID | Use |
|---|---|
| `0x010` | Default TC target (test/sim) |
| `0xFFFF` | Wildcard — matches any APID |

---

## Integrating openobsw into a mission

1. Define all mission parameters above for your spacecraft.
2. Set SCID and VCID in your `obsw_tm_frame_config_t` instance.
3. Allocate APIDs and populate your `obsw_tc_route_t` routing table.
4. Override compile-time sizing macros in your `CMakeLists.txt` if needed.
5. Implement `obsw_io_ops_t` for your target platform (UART, SpaceWire, etc).
6. Update this document with your mission's actual values before flight.

---

## Summary table

| Parameter | openobsw default | Mission value |
|---|---|---|
| SCID | `0x001` | _to be defined_ |
| VCID (TC) | `0x00` | _to be defined_ |
| VCID (TM) | `0x00` | _to be defined_ |
| TM frame data field length | `64` bytes | _to be defined_ |
| TC frame max length | `1024` bytes | _to be defined_ |
| TM store slots | `32` | _to be defined_ |
| TM max packet length | `1024` bytes | _to be defined_ |
| TC APID (nominal) | `0x010` | _to be defined_ |