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

> **Timing:** `0xFF` is sent immediately after the TC is queued — before PUS
> has processed it. Ground tools must read for ~300 ms after the first `0xFF`
> to catch the TM response that arrives via the PUS notification path.

**UART RX pull-up:** `uart_init()` enables the STM32H7 internal pull-up on
PD9 (RX). Without it a floating/disconnected RX line generates continuous
false RXNE events; TMTC spins at priority 4 and starves FDIR, halting the
IWDG kick and causing a 4 s reset loop.

**`taskYIELD()` after each frame:** Inserted after every frame (valid or
invalid) to guarantee FDIR and lower-priority tasks get a scheduling slot
even under continuous UART traffic.

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
2. **LCD re-init (first tick only):** The ST7735R internal power supervisor
   fires during the FreeRTOS startup current spike (~1 s after boot), clearing
   GRAM and resetting the controller state. FDIR re-runs `lcd_init()` +
   `lcd_console_init()` at tick 1 to restore the display. `lcd_init()` no
   longer drives the backlight (`BL_ON` is called once from `main()` via
   `lcd_backlight_on()`); FDIR re-init never touches the backlight line,
   avoiding a second current spike that would re-trigger the power supervisor.
3. **Boot event** — on first tick only (after LCD re-init), emits
   `SRDB_EVENT_BOOT_COMPLETE` (INFO).
4. **Mode change detection** — if FSM mode changed since last tick, emits
   `SRDB_EVENT_SAFE_MODE_ENTRY` (HIGH) or `SRDB_EVENT_SAFE_MODE_EXIT` (INFO).
5. **LCD status bar update** — draws `FDIR:NOMINAL  WDG:XXXXXXXX` (or
   `FDIR:SAFE`) on the bottom row using `lcd_console_set_status()`.

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

## LCD console

The ST7735R LCD driver (`hal/stm32h7/lcd.c`, `hal/stm32h7/lcd_console.c`)
provides a scrolling text console (160×80, 26×10 chars, 5×7 font).

### Backlight and power supervisor

`lcd_init()` configures the controller but **does not** enable the backlight.
Call `lcd_backlight_on()` once from `main()` after the first `lcd_init()`.
Never call it from a task — toggling the backlight after FreeRTOS is running
causes a current spike that triggers the ST7735R internal power supervisor,
clearing GRAM and resetting config registers.

### Startup sequence

| Time | Event |
|---|---|
| T = 0 ms | MCU boot; `lcd_init()` + `lcd_backlight_on()` in `main()` |
| T ≈ 350 ms | Boot messages displayed (`openobsw vX.Y.Z`, clock, `FreeRTOS starting...`) |
| T ≈ 350–1000 ms | ST7735R power supervisor may fire → screen goes black. Backlight stays on. |
| T = 1000 ms | FDIR tick 1: `lcd_init()` (no BL) + `lcd_console_init()` + messages → display stable |
| T > 1000 ms | FDIR updates status bar every second |

### Thread safety

`lcd_console_puts()` and `lcd_console_set_status()` wrap each character draw
in `taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()`. This prevents SysTick
from preempting mid-SPI-transfer while still allowing inter-character
scheduling. Only the FDIR task calls LCD functions after the scheduler starts.

---

## Fault handlers

`platform/stm32h7/freertos_hooks.c` overrides the weak default handlers:

| Handler | Behaviour |
|---|---|
| `HardFault_Handler` | Naked trampoline — captures MSP/PSP, calls `hard_fault_handler_c()` which prints `PC`, `LR`, `HFSR`, `CFSR`, `MMAR`, `BFAR` to UART then spins (IWDG resets after 4 s) |
| `vApplicationStackOverflowHook` | Prints task name to UART then spins |
| `obsw_freertos_assert_fail` | Prints file + line to UART then spins |

All three write directly to USART3 TDR (no driver dependency) so they work
regardless of scheduler state.

> **Previous bug:** the original `obsw_freertos_assert_fail` executed
> `bkpt #0`. Without a debugger attached, `bkpt` triggers a HardFault; the
> old `HardFault_Handler` was `b .` (spin). Any FreeRTOS assert → bkpt →
> HardFault → spin → FDIR starved → IWDG fires → silent reset loop.

---

## Ground tools

| Tool | Path | Purpose |
|---|---|---|
| `ping_uart.py` | `tools/ping_uart.py` | Send TC(17,1) ping over CP2102 UART; decode TM(1,1), TM(17,2), TM(1,7) |
| `send_ping.py` | `sim/send_ping.py` | Send TC(17,1) ping to host sim subprocess |

```bash
# Requires: pip install pyserial
python3 tools/ping_uart.py /dev/ttyUSB0          # 3 pings, auto-parse TM
python3 tools/ping_uart.py /dev/ttyUSB0 --raw    # raw hex dump for debugging
```

**Frame format:** the dispatcher receives a raw **PUS-C space packet**
(11 bytes), not a TC transfer frame. Wire protocol carries space packets
directly — the TC frame decode layer is only used in the integration test.

```
18 01  C0 00  00 04  11  11  01  00 00
  │       │      │   │   │   │    └─ source ID
  │       │      │   │   │   └─ subservice = 1
  │       │      │   │   └─ service = 17
  │       │      │   └─ PUS-C secondary hdr (ver+ack)
  │       │      └─ data_len = 4  (→ payload_len = 5 ≥ PUS_TC_SEC_HDR_LEN)
  │       └─ seq: standalone, count 0
  └─ packet ID: TC, sec_hdr=1, APID=0x001
```

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

| Item | Milestone | Notes |
|---|---|---|
| TC/TM end-to-end ping from CP2102 | v0.8 #49 | `tools/ping_uart.py` in progress; TM response debugging ongoing |
| S3 housekeeping (FreeRTOS path) | v0.8 #51 | PUS task has S1/S8/S17/S20 but not S3; needs FreeRTOS software timers |
| I2C HAL (`obsw_i2c_ops_t`) | v0.10 #37 | Not yet implemented |
| QMC5883L magnetometer driver | v0.10 #38 | Hardware owned; stubs return `mag_valid = false` |
| ICM-42688 gyroscope driver | v0.10 #53 | Not yet implemented |
| INA219 power monitor | v0.10 #39 | Not yet implemented |
| MTQ / RW actuator output | v0.10 | AOCS computes commands but does not write to hardware |
| STM32H750 Renode socket transport for OpenSVF | v0.11 #54 | Renode script exists; OpenSVF integration not yet validated |
