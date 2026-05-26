# v0.8 FreeRTOS Task Architecture — STM32H750

## Overview

v0.8 replaces the blocking superloop on the STM32H750 target with a
FreeRTOS task architecture that matches real flight OBSW structure.
The MSP430 and host sim targets are unaffected.

**Design constraints carried forward:**
- Zero dynamic allocation — FreeRTOS configured with static allocation only
  (`configSUPPORT_STATIC_ALLOCATION=1`, `configSUPPORT_DYNAMIC_ALLOCATION=0`)
- No global state — all context passed explicitly; shared state protected by mutexes
- HAL-isolated — new I2C HAL follows the same `obsw_i2c_ops_t` vtable pattern as `obsw_io_ops_t`

---

## Task architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        STM32H750                            │
│                                                             │
│  ┌──────────────┐   TC queue    ┌──────────────┐           │
│  │  TMTC Task   │ ────────────► │   PUS Task   │           │
│  │  (High)      │               │  (Med-High)  │           │
│  │              │ ◄──────────── │              │           │
│  │ UART RX/TX   │   TM store    │  S1–S20      │           │
│  └──────────────┘   (mutex)     └──────┬───────┘           │
│                                        │ event group        │
│  ┌──────────────┐                ┌─────▼───────┐           │
│  │  AOCS Task   │                │  FDIR Task  │           │
│  │  (Medium)    │ ──────────────►│  (Med-High) │           │
│  │              │  health flags  │             │           │
│  │  10 Hz tick  │                │  watchdog   │           │
│  └──────────────┘                └─────────────┘           │
└─────────────────────────────────────────────────────────────┘
```

---

## Task definitions

### TMTC Task — `task/tmtc.c`

| Property | Value |
|---|---|
| Priority | High (configMAX_PRIORITIES − 1) |
| Trigger | UART RX interrupt → byte queue |
| Stack | Statically allocated |

**Uplink:** UART RX ISR feeds a byte queue. Task assembles wire-protocol v3
frames, CRC-checks TC frames, puts `obsw_tc_t` onto the TC queue for the
PUS task.

**Downlink:** Drains the TM store (mutex-protected), builds TM frames via
`obsw_tm_frame_build()`, transmits via UART.

---

### PUS Task — `task/pus.c`

| Property | Value |
|---|---|
| Priority | Medium-high |
| Trigger | Blocks on TC queue from TMTC |
| Stack | Statically allocated |

Consumes TC packets from the queue, runs the service dispatcher (S1–S20),
produces TM packets into the TM store. S3 housekeeping reports are
scheduled here via a periodic software timer.

---

### AOCS Task — `task/aocs.c`

| Property | Value |
|---|---|
| Priority | Medium |
| Trigger | Periodic — `vTaskDelayUntil()` at fixed rate (10 Hz) |
| Stack | Statically allocated |

Calls `obsw_aocs_step()` each tick. Reads from the shared sensor state
(mutex-protected, populated by the sensor poll inside this task or a
dedicated driver). Writes actuator commands to PWM/GPIO HAL.

The AOCS algorithm is kept behind a clean `Init / Step / Terminate`
interface so a Simulink Embedded Coder drop-in replacement is possible
without changing the task:

```c
void obsw_aocs_init(obsw_aocs_ctx_t *ctx);
void obsw_aocs_step(obsw_aocs_ctx_t *ctx,
                    const obsw_sensor_frame_t *sensors,
                    obsw_actuator_frame_t     *actuators);
```

---

### FDIR Task — `task/fdir.c`

| Property | Value |
|---|---|
| Priority | Medium-high |
| Trigger | Event group (S5 severity bits) + periodic watchdog kick |
| Stack | Statically allocated |

Monitors health flags set by the PUS and AOCS tasks via an
`EventGroupHandle_t`. Manages SAFE ↔ NOMINAL mode transitions through
the existing `obsw_fsm_ctx_t`. Kicks the IWDG hardware watchdog each
cycle — a stuck FDIR task causes a hardware reset.

---

## Inter-task communication

All handles are statically allocated at file scope in `platform/stm32h7/main.c`.

| Channel | Type | Producers | Consumers | Notes |
|---|---|---|---|---|
| TC queue | `QueueHandle_t` (static) | TMTC | PUS | Fixed-depth, `obsw_tc_t` items |
| TM store | `obsw_tm_store_t` + mutex | PUS, AOCS | TMTC | Existing ring buffer, add `SemaphoreHandle_t` |
| FDIR event group | `EventGroupHandle_t` (static) | PUS (S5 events) | FDIR | Bit per severity level |
| Sensor state | Shared struct + mutex | AOCS (writer) | PUS/S3 (reader) | Latest sensor values for HK |

---

## Priority table

| Task | Priority | Rationale |
|---|---|---|
| TMTC | Highest | Must not drop TC frames or stall TM downlink |
| FDIR | Med-high | Fault response must preempt normal processing |
| PUS | Med-high | TC processing should complete promptly |
| AOCS | Medium | Fixed-rate control, can miss at most 1 tick gracefully |
| Idle | Lowest | FreeRTOS idle task |

---

## Static allocation policy

Every FreeRTOS object is created with its `Static` variant:

```c
/* Example: TC queue */
static StaticQueue_t    tc_queue_buf;
static obsw_tc_t        tc_queue_storage[TC_QUEUE_DEPTH];
QueueHandle_t tc_queue = xQueueCreateStatic(
    TC_QUEUE_DEPTH, sizeof(obsw_tc_t),
    (uint8_t *)tc_queue_storage, &tc_queue_buf);

/* Example: task */
static StaticTask_t     pus_task_buf;
static StackType_t      pus_task_stack[PUS_STACK_DEPTH];
xTaskCreateStatic(pus_task_fn, "PUS", PUS_STACK_DEPTH,
                  NULL, PUS_PRIORITY,
                  pus_task_stack, &pus_task_buf);
```

`configASSERT()` is enabled in debug builds. Stack overflow detection
(`configCHECK_FOR_STACK_OVERFLOW=2`) is enabled for all tasks.

---

## New HAL surface: `obsw_i2c_ops_t`

Sensors (QMC5883L, INA219) share the I2C1 bus (PB8 SCL / PB9 SDA).
A new vtable follows the same pattern as `obsw_io_ops_t`:

```c
typedef struct {
    int (*write)(void *ctx, uint8_t addr, const uint8_t *buf, uint16_t len);
    int (*read) (void *ctx, uint8_t addr,       uint8_t *buf, uint16_t len);
} obsw_i2c_ops_t;
```

The concrete STM32H750 implementation is in `hal/stm32h7/i2c.c`.
A `SemaphoreHandle_t` mutex guards the shared bus — AOCS and PUS tasks
both read from it at different rates.

---

## Build changes

The FreeRTOS port is pulled in via CMake `FetchContent` (same pattern as
Unity in the test build). The STM32H750 target gains:

```cmake
option(OBSW_FREERTOS "Enable FreeRTOS task architecture" OFF)
```

The superloop path remains compilable (`OBSW_FREERTOS=OFF`) for reference
and Renode smoke-testing until the FreeRTOS build is fully validated.

---

## Scope

| In scope for v0.8 | Out of scope |
|---|---|
| FreeRTOS CMake integration | MSP430 FreeRTOS port |
| TMTC, PUS, AOCS, FDIR tasks | ZynqMP RTOS |
| `obsw_i2c_ops_t` HAL + STM32H750 impl | Simulink AOCS drop-in |
| QMC5883L driver (mag → B-dot) | GPS / S9 time management |
| INA219 driver (power → S3 HK) | N20 motor / encoder driver |
| `obsw_aocs_step()` interface refactor | Star tracker interface |
| Stack overflow detection | Over-the-air update |
