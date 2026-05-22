# opensvf Track B — Handoff context

**Companion repo:** `../openobsw`
**openobsw branch:** main (commit after 2026-05-22 "feat(srdb): add XTCE codegen…")

This document gives you everything needed to fix the three integration gaps in
`OBCEmulatorAdapter` so opensvf can drive the real openobsw binary end-to-end.

---

## What openobsw now emits

The sim binary (`obsw_sim`) sends three TM(3,25) HK sets periodically.
The one opensvf needs is **set_id = 3** (`dhs_obc_hk`, SPID 1003).

### Wire protocol (unchanged)

stdin/stdout pipe, type-prefixed frames:

```
SVF → OBSW:   [0x01][uint16 BE len][PUS TC bytes]       TC uplink
              [0x02][uint16 BE len][sensor_frame_t]      Sensor injection

OBSW → SVF:   [0x04][uint16 BE len][PUS TM bytes]       TM downlink
              [0x03][uint16 BE len][actuator_frame_t]    Actuator output
              [0xFF]                                      End-of-tick sync
```

### TM(3,25) DHS OBC HK packet — APID=0x010, svc=3, subsvc=25, HK_SID=3

Packet bit layout (offsets from packet start):

| Bits | Field | Notes |
|------|-------|-------|
| 0–47 | CCSDS primary header | version/type/APID/seq/len |
| 48–55 | PUS version + spare | 0x20 for PUS-C |
| 56–63 | Service type | = 3 |
| 64–71 | Subservice type | = 25 |
| 72–87 | Message counter | per-service uint16 |
| 88–103 | Destination ID | uint16 |
| 104–135 | Timestamp | CUC 4-byte uint32 |
| **136–143** | **HK_SID** | **= 3 — identifies this set** |
| **144–151** | **OBC_MODE** | uint8, enum |
| **152–183** | **OBC_OBT** | uint32, seconds |
| **184–191** | **OBC_WATCHDOG_STATUS** | uint8, enum |
| **192–199** | **OBC_MEMORY_USED_PCT** | uint8, % |
| **200–207** | **OBC_HEALTH** | uint8, enum |
| **208–223** | **OBC_RESET_COUNT** | uint16, count |
| **224–231** | **OBC_CPU_LOAD** | uint8, % |

Application data field starts at byte 17 (bit 136).
Byte offsets within application data: HK_SID at +0, OBC_MODE at +1, OBC_OBT at +2, OBC_WATCHDOG_STATUS at +6, OBC_MEMORY_USED_PCT at +7, OBC_HEALTH at +8, OBC_RESET_COUNT at +9, OBC_CPU_LOAD at +11.

```python
import struct

APP_DATA_OFFSET = 17  # bytes from packet start

def parse_dhs_obc_hk(pkt: bytes) -> dict:
    app = pkt[APP_DATA_OFFSET:]
    sid = app[0]
    assert sid == 3
    (mode, obt, wd, mem, health, reset, cpu) = struct.unpack_from(
        ">BIBBBHb", app, 1  # 1 byte past sid; H=reset_count is uint16
    )
    # Note: OBC_RESET_COUNT is big-endian uint16; OBC_CPU_LOAD is uint8
    # Correct unpack:
    mode, obt, wd, mem, health, reset, cpu = struct.unpack_from(
        ">BIBBBBHB", app, 1
    )
    return {
        "obc_mode":             mode,   # 0=SAFE, 1=NOMINAL, 2=PAYLOAD
        "obc_obt":              obt,    # on-board time [s]
        "obc_watchdog_status":  wd,     # 0=NOMINAL, 1=EXPIRED
        "obc_memory_used_pct":  mem,    # %
        "obc_health":           health, # 0=NOMINAL, 1=DEGRADED, 2=FAILED
        "obc_reset_count":      reset,  # count
        "obc_cpu_load":         cpu,    # %
    }
```

Wait — the correct struct format for the application data after HK_SID:
- OBC_MODE: B (1 byte)
- OBC_OBT: >I (4 bytes, big-endian uint32)
- OBC_WATCHDOG_STATUS: B (1 byte)
- OBC_MEMORY_USED_PCT: B (1 byte)
- OBC_HEALTH: B (1 byte)
- OBC_RESET_COUNT: >H (2 bytes, big-endian uint16)
- OBC_CPU_LOAD: B (1 byte)

```python
import struct

def parse_dhs_obc_hk(pkt: bytes) -> dict | None:
    """Extract DHS OBC HK parameters from a raw TM(3,25) set_id=3 packet."""
    if len(pkt) < 29:
        return None
    app = pkt[17:]         # skip primary (6) + secondary (11) headers
    if app[0] != 3:        # HK_SID check
        return None
    mode, obt, wd, mem, health, reset, cpu = struct.unpack_from(">BIBBBBHB", app, 1)
    return {
        "obc_mode":            mode,
        "obc_obt":             obt,
        "obc_watchdog_status": wd,
        "obc_memory_used_pct": mem,
        "obc_health":          health,
        "obc_reset_count":     reset,
        "obc_cpu_load":        cpu,
    }
```

### Enumerations

| Parameter | 0 | 1 | 2 |
|-----------|---|---|---|
| `obc_mode` | SAFE | NOMINAL | PAYLOAD |
| `obc_watchdog_status` | NOMINAL | EXPIRED | — |
| `obc_health` | NOMINAL | DEGRADED | FAILED |

### What's live vs. stubbed in the sim

| Parameter | Source in obsw_sim |
|-----------|--------------------|
| `obc_mode` | Real — FSM state (`obsw_fsm_is_safe()`) |
| `obc_obt` | Real — `sensor.sim_time` cast to uint32 |
| `obc_watchdog_status` | Always 0 (WD kicked every tick) |
| `obc_memory_used_pct` | Stub = 0 |
| `obc_health` | Stub = 0 |
| `obc_reset_count` | Stub = 0 |
| `obc_cpu_load` | Stub = 0 |

---

## XTCE database

openobsw builds `build/xtce/mission.xtce` — a complete XTCE 1.2 document.
Load it directly into YAMCS. The `generate_xtce.py` in opensvf can be retired
or pointed at this file.

### Using the SRDB Python package

`obsw_srdb` is already installed in opensvf's venv as a path dependency.
Use it instead of hardcoding any parameter names:

```python
from obsw_srdb import SRDBLoader

srdb = SRDBLoader.load("../openobsw/srdb/data")

# Get the DHS OBC HK set definition
hk = srdb.hk_set_by_id(3)       # dhs_obc_hk, SPID 1003
hk.parameters                    # ['obc_mode', 'obc_obt', ...]

# Get a parameter by name
p = srdb.parameter_by_name("obc_mode")
p.id          # 0x0010
p.ptc, p.pfc  # 1, 8  (ECSS uint8)
p.enumeration # [EnumEntry(0, 'SAFE'), EnumEntry(1, 'NOMINAL'), ...]
```

---

## The three gaps to fix in opensvf

### Gap 1 — TM queueing (fix first, no coordination needed, highest impact)

**File:** `src/svf/models/dhs/obc_emulator.py`

`get_tm_queue()` returns `[]`. `_parse_tm()` parses TM into `ParameterStore`
but never appends to `self._tm_queue`. Any campaign procedure calling
`ctx.expect_tm()` times out even when TM was correctly received and decoded.

**Fix:** in `_parse_tm()`, after extracting each packet, append it:

```python
def _parse_tm(self, pkt: bytes) -> None:
    # ... existing ParameterStore logic ...
    self._tm_queue.append(pkt)   # ADD THIS
```

---

### Gap 2 — Hardcoded `dhs.obc.*` parameter names

**File:** `src/svf/models/dhs/obc_emulator.py`, `do_step()`

Currently hardcodes strings like `"dhs.obc.mode"`, `"dhs.obc.obt"`, etc.
These never matched openobsw's SRDB. Now that Track A is done, the
canonical names are `"obc_mode"`, `"obc_obt"`, `"obc_watchdog_status"`,
`"obc_memory_used_pct"`, `"obc_health"`, `"obc_reset_count"`, `"obc_cpu_load"`.

**Fix approach:**

1. Identify incoming TM packets as DHS OBC HK by: service=3, subservice=25, HK_SID=3
2. Unpack by byte offset (see `parse_dhs_obc_hk()` above)
3. Store under the SRDB parameter names — either hardcode the correct names
   or load them from `obsw_srdb`:

```python
from obsw_srdb import SRDBLoader

_srdb = SRDBLoader.load("../openobsw/srdb/data")
_DHS_OBC_HK = _srdb.hk_set_by_id(3)
_DHS_OBC_PARAMS = [
    _srdb.parameter_by_name(n) for n in _DHS_OBC_HK.parameters
]
```

In `do_step()`, replace references to `"dhs.obc.mode"` etc. with
`"obc_mode"` etc. (the SRDB `name` field, not any dotted path).

---

### Gap 3 — No sync byte error recovery (low priority)

**File:** `src/svf/models/dhs/obc_emulator.py`, `_collect_until_sync()`

On desync the method logs a warning and continues silently. The sim
can drift indefinitely without the campaign knowing.

**Fix:** count consecutive missed syncs; raise `EquipmentTickError` after
a threshold (e.g. 3):

```python
MAX_DESYNC = 3

def _collect_until_sync(self) -> list[bytes]:
    missed = 0
    while True:
        byte = self._read_byte()
        if byte == 0xFF:
            missed = 0
            return self._frames
        missed += 1
        if missed >= MAX_DESYNC:
            raise EquipmentTickError(
                f"Lost sync: {missed} consecutive frames without 0xFF"
            )
        self._log.warning("sync missed (%d)", missed)
```

---

## Key opensvf files

| File | Role |
|------|------|
| `src/svf/models/dhs/obc_emulator.py` | Main fix target — all three gaps |
| `src/svf/models/dhs/hil_adapter.py` | HilAdapter ABC — interface to satisfy |
| `src/svf/models/dhs/obc.py` | Software OBC stub — reference for expected behaviour |
| `src/svf/pus/services.py` | PUS S1/S3/S5/S17/S20 parsing |
| `mission_mysat1/spacecraft.yaml` | Equipment list and `obsw.type` setting |
| `mission_mysat1/campaigns/platform_campaign.yaml` | End-to-end validation entry point |
| `tests/system/test_obc_emulator.py` | System tests — run these after each gap fix |

## S5 event IDs (already correct in openobsw — no change needed)

| Event ID | Meaning |
|----------|---------|
| `0x0002` | Transition to SAFE mode |
| `0x0003` | Transition to NOMINAL mode |

opensvf's S5 parser should already handle these correctly.

---

## Suggested fix order

1. **Gap 1** — TM queueing. One-line fix. Run `test_obc_emulator.py` after.
2. **Gap 2** — Replace hardcoded names with SRDB names + byte-offset extraction.
3. **Gap 3** — Sync error recovery. Add after end-to-end campaign is passing.
