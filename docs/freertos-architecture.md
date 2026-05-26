# FreeRTOS Task Architecture — STM32H750 (v0.8)

## Overview

v0.8 replaces the blocking superloop on the STM32H750 target with a four-task
FreeRTOS architecture. The MSP430 and host sim targets are unaffected.

**Design constraints:**
- Zero dynamic allocation — all FreeRTOS objects use `*Static()` variants
  (`configSUPPORT_STATIC_ALLOCATION=1`, `configSUPPORT_DYNAMIC_ALLOCATION=0`)
- No global state — contexts passed explicitly; there is no mutex on the TM
  store (see inter-task communication)
- HAL-isolated — all I/O goes through `obsw_io_ops_t`

---

## Task architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        STM32H750                            │
│                                                             │
│  ┌──────────────┐   TC queue    ┌──────────────┐           │
│  │  TMTC Task   │ ────────────► │   PUS Task   │           │
│  │  (prio 4)    │               │  (prio 3)    │           │
│  │              │ ◄──────────── │              │           │
│  │ UART RX/TX   │  task notify  │  S1/S8/S17/  │           │
│  └──────────────┘               │  S20 + FSM   │           │
│                                 └──────┬───────┘           │
│  ┌──────────────┐                      │ FSM ptr            │
│  │  AOCS Task   │          ┌───────────▼───────┐           │
│  │  (prio 2)    │          │   FDIR Task       │           │
│  │              │          │   (prio 3)        │           │
│  │  10 Hz tick  │          │   IWDG + FSM owner│           │
│  └──────────────┘          └───────────────────┘           │
└─────────────────────────────────────────────────────────────┘
```

---

## Task definitions

### TMTC Task — `task/tmtc.c`

| Property | Value |
|---|---|
| Priority | 4 (highest user task) |
| Trigger | UART RXNE polling; woken by PUS task notify |
| Stack | Static |

Owns UART I/O. Polls the USART3 RXNE bit directly; when no byte is available
it yields for 1 ms via `ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(1))`. The PUS
task calls `xTaskNotify(tmtc_handle, ...)` after each TC dispatch so TMTC
wakes immediately to drain the TM store.

**Uplink:** Reads wire-protocol v3 frames (`[type][uint16 BE len][body]`).
TC frames (type `0x01`) are placed on the TC queue for the PUS task as
`obsw_tc_frame_item_t` items (raw frame + length, up to 512 B). Other frame
types are silently discarded.

**Downlink:** After each inbound frame (and on PUS notify), drains
`obsw_tm_store_t` and outputs each TM packet as `[0x04][len_hi][len_lo][bytes]`.
Writes `0xFF` end-of-tick after each inbound frame.

---

### PUS Task — `task/pus.c`

| Property | Value |
|---|---|
| Priority | 3 |
| Trigger | Blocks on TC frame queue from TMTC |
| Stack | Static |

Consumes `obsw_tc_frame_item_t` items from the TMTC queue. Before dispatching,
applies the FSM TC gate:

```c
if (!obsw_fsm_tc_allowed(fsm, frame[12], frame[13]))
    continue;   /* drop TC in SAFE mode if not whitelisted */
```

Service/subservice are read from known byte offsets (5-byte TC transfer frame
header + 6-byte CCSDS primary header + 1-byte PUS version/ack byte → service
at byte [12], subservice at [13]).

After the gate, the frame is fed to the TC dispatcher (S1, S8, S17, S20).
S8 function ID 1 (`OBSW_S8_FN_RECOVER_NOMINAL`) calls `obsw_fsm_to_nominal()`
on the FSM owned by the FDIR task. After each dispatch, TMTC is notified to
flush the TM store.

---

### AOCS Task — `task/aocs.c`

| Property | Value |
|---|---|
| Priority | 2 (lowest user task) |
| Trigger | Periodic — `vTaskDelayUntil()` at 10 Hz |
| Stack | Static |

Runs at exactly 10 Hz. Reads the current FSM mode and applies the appropriate
control law:

| Condition | Algorithm | Input | Output |
|---|---|---|---|
| NOMINAL, `st_valid && gyro_valid` | PD quaternion (`adcs.c`) | Star tracker + gyro | RW torque commands |
| Any other, `mag_valid` | B-dot (`bdot.c`) | Magnetometer | MTQ dipole commands |
| No valid sensors | None | — | Actuators hold last command |

B-dot state is reset on each NOMINAL → SAFE transition. Controller configs
match the host sim: `gain = 1e4 A·m²·s/T`, `max_dipole = 10 A·m²`,
`kp = 0.5`, `kd = 0.1`, `max_torque = 0.01 N·m`, `dt = 0.1 s`.

**Sensor hardware not yet connected.** All sensor reads are stubs returning
`valid = false`. Actuators produce no output until I2C drivers are wired
(see [What is not yet wired](#what-is-not-yet-wired)).

---

### FDIR Task — `task/fdir.c`

| Property | Value |
|---|---|
| Priority | 3 |
| Trigger | Periodic — `vTaskDelayUntil()` at 1 Hz |
| Stack | Static |

Owns the `obsw_fsm_ctx_t` and the hardware IWDG. Init order in `main.c`
requires FDIR to initialise before PUS (PUS receives the FSM pointer via
`obsw_fdir_get_fsm()`).

Each 1 s tick:

1. **Kick IWDG** (`IWDG_KR = 0xAAAA`) — 4 s timeout (prescaler /128 from
   LSI ≈ 32 kHz, reload 1000).
2. **Boot event** — on first tick only, emits `SRDB_EVENT_BOOT_COMPLETE` (INFO).
3. **Mode change detection** — if FSM mode changed since last tick, emits
   `SRDB_EVENT_SAFE_MODE_ENTRY` (HIGH) or `SRDB_EVENT_SAFE_MODE_EXIT` (INFO).

A stuck FDIR task causes IWDG expiry and hardware reset within 4 s.

TC whitelist in SAFE mode: `{8,1}` (S8 recover), `{17,1}` (ping), `{20,3}`
(parameter get).

---

## Inter-task communication

| Channel | Type | Producer | Consumer | Notes |
|---|---|---|---|---|
| TC queue | `QueueHandle_t` static, 4 slots | TMTC | PUS | `obsw_tc_frame_item_t` — raw frame + 16-bit length |
| TM store | `obsw_tm_store_t` SPSC ring, 32 slots | PUS | TMTC | Lock-free `volatile` head/tail; safe on single-core FreeRTOS — context switches (PendSV) include ARM memory barriers |
| Task notify | `xTaskNotify` | PUS | TMTC | Immediate TM drain after each TC dispatch |
| FSM pointer | `obsw_fsm_ctx_t *` | FDIR (owner) | PUS (gate + S8) | Set once at init; FSM transitions only from PUS task — no concurrent write conflict |

---

## Priority table

| Task | Priority | Rationale |
|---|---|---|
| TMTC | 4 — highest | Must not drop TC frames or stall TM downlink |
| FDIR | 3 | IWDG kick and fault response must complete each cycle |
| PUS | 3 | TC processing should complete promptly |
| AOCS | 2 | Fixed-rate control; can be preempted without losing correctness |
| Idle | 0 | FreeRTOS idle task |

---

## Static allocation

All FreeRTOS objects use `*Static()` variants. `configASSERT()` is enabled in
all builds. Stack overflow detection (`configCHECK_FOR_STACK_OVERFLOW=2`) is
defined in `platform/stm32h7/freertos_hooks.c`.

---

## Build

```bash
cmake -S targets/stm32h7 -B build_stm32h7 \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_TOOLCHAIN_FILE=cmake/stm32h7-toolchain.cmake \
    -DOBSW_ROOT=$(pwd) \
    -DOBSW_FREERTOS=ON
cmake --build build_stm32h7
```

The superloop (`OBSW_FREERTOS=OFF`) is kept compilable for Renode smoke-tests.

FreeRTOS V11.1.0 is fetched via `FetchContent` at configure time using port
`GCC_ARM_CM7`. `libm` (`acosf`, `sqrtf`) requires a bare-metal `__errno()`
stub (`platform/stm32h7/errno_stub.c`) because newlib-nano does not provide it.

---

## What is not yet wired

| Item | Notes |
|---|---|
| QMC5883L magnetometer driver | Hardware arriving; stubs return `mag_valid = false` |
| MTQ / RW actuator output | AOCS computes commands but does not write to hardware |
| I2C HAL (`obsw_i2c_ops_t`) | Not yet implemented |
| INA219 power monitor | Not yet implemented — needed for S3 power HK |
| S3 housekeeping from AOCS | Not yet implemented |
