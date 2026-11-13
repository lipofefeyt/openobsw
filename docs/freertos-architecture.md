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

Consumes `obsw_tc_frame_item_t` items from the TMTC queue and feeds each to
the TC dispatcher (S3, S6, S8, S9, S17, S20). The dispatcher applies the FSM
TC gate before route lookup:

```c
obsw_tc_dispatcher_init(&d, routes, n, obsw_s1_dispatch_responder, &s1_ctx);
obsw_tc_dispatcher_set_gate(&d, obsw_fsm_tc_gate, obsw_mode_get_fsm());
```

A TC refused by the gate in SAFE/STANDBY gets TM(1,2) code `0x0007`; an
unrouted TC gets `0x0002` (unknown service) or `0x0003` (unknown subservice).
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

**QMC5883L magnetometer is wired** (I2C1, PB8/PB9, 100 kHz). `mag_valid` is
true when the sensor chip-ID checks out and each read returns data.
After each control tick the raw field `b[3]` and B-dot dipole command
`out.m_cmd[3]` are written to `obsw_aocs_hk_t` (see
`include/obsw/task/aocs.h`) for S3 HK reporting.

MTQ hardware drivers are not yet wired — `out.m_cmd` is computed by `bdot.c`
but the GPIO/PWM output to physical magnetorquers is pending (see
[What is not yet wired](#what-is-not-yet-wired)).

---

### FDIR Task — `task/fdir.c`

| Property | Value |
|---|---|
| Priority | 3 |
| Trigger | Periodic — `vTaskDelayUntil()` at 1 Hz |
| Stack | Static |

Owns the `obsw_fsm_ctx_t`, the hardware IWDG, and the **S3 HK context**. Init
order in `main.c` requires FDIR to initialise before PUS (PUS receives the FSM
pointer via `obsw_mode_get_fsm()` and the S3 pointer via
`obsw_fdir_get_s3_ctx()`).

Each 1 s tick:

1. **Kick IWDG** (`IWDG_KR = 0xAAAA`) — 4 s timeout (prescaler /128 from
   LSI ≈ 32 kHz, reload 1000).
2. **Boot event** — on first tick only, emits
   `SRDB_EVENT_BOOT_COMPLETE` (INFO).
3. **Mode change detection** — if the FSM entered SAFE since the last tick,
   increments `s_safe_mode_count` (HK). The mode events themselves come from
   the Mode Manager task, at its next 1 s tick: `SRDB_EVENT_SAFE_MODE_ENTRY`
   (HIGH), `SRDB_EVENT_STANDBY_ENTRY` or `SRDB_EVENT_NOMINAL_ENTRY` (INFO),
   without auxiliary data (OBSW-SRS-190). The host sim reports the same three.
4. **S3 HK tick** — calls `obsw_s3_tick()` advancing all enabled HK set
   countdowns. Enabled sets emit TM(3,25) when their countdown reaches zero.
5. **LCD dashboard** — builds an `obsw_dash_snapshot_t` (mode, OBT, reset
   cause, TMTC link statistics, AOCS HK), lays it out with
   `obsw_dash_format()` and draws it with `lcd_dash_render()`, which redraws
   only the cells that changed. Every 30 s the whole grid is redrawn as a
   self-heal. See [LCD dashboard](#lcd-dashboard).

A stuck FDIR task causes IWDG expiry and hardware reset within 4 s.

TC whitelist in SAFE and STANDBY: `{17,1}` (ping), `{8,1}` (S8 mode
functions), `{9,128}` (set OBT), `{20,3}` (parameter get). It is generated
from `safe_allowed: true` in `srdb/data/telecommands.yaml` as
`SRDB_SAFE_TC_WHITELIST` and shared by every target.

---

## Inter-task communication

| Channel | Type | Producer | Consumer | Notes |
|---|---|---|---|---|
| TC queue | `QueueHandle_t` static, 4 slots | TMTC | PUS | `obsw_tc_frame_item_t` — raw frame + 16-bit length |
| TM store | `obsw_tm_store_t` ring, 32 slots | PUS + FDIR | TMTC | Lock-free `volatile` head/tail. Two same-priority writers (PUS prio 3, FDIR prio 3) never preempt each other mid-enqueue on single-core; TMTC (prio 4) only advances `tail`, not `head`. |
| AOCS HK | `obsw_aocs_hk_t` volatile struct | AOCS | FDIR (S3) | Each field is a single 32-bit write — atomic on Cortex-M7. `mag_valid` written last so reader sees consistent data when valid=1. |
| Task notify | `xTaskNotify` | PUS | TMTC | Immediate TM drain after each TC dispatch |
| Mode FSM | `obsw_fsm_ctx_t` in `mode.c` | Mode Manager (requests, LEOP timer); FDIR task via S12 (HIGH safe-trigger event → SAFE) | PUS (TC gate), AOCS, FDIR | Two writers. Every transition is made inside a critical section: the Mode Manager around its request and timer block, the FDIR task inside `obsw_pus_s12_check()`. Readers take single-word reads of `mode`. A SAFE entry made by S12 is also filed with `obsw_mode_request_safe()`, which discards a recover-to-NOMINAL request pending from before the fault. |

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

## LCD dashboard

The ST7735R driver (`hal/stm32h7/lcd.c`) drives the 160×80 panel (26×10
cells of 6×8 px, 5×7 font). FDIR shows a fixed-layout dashboard, refreshed
once per second:

```
openobsw v0.1.0 STM32H750      title bar (cyan)
OBT 00001234  RST POR          on-board time, last reset cause
TC rx 000012  rej 000001       TC frames received, TM(1,2)+TM(1,8) sent
TM tx 000042  last 17,2        TM packets sent, last TM (svc,subsvc)
EVT 0x0001 INFO                last TM(5,x) event ID and severity
AOCS BDOT     MAG ok           control law run on the last AOCS tick
B uT   +21.3   -4.0  +38.9     magnetometer field
M Am2  +0.12  -0.03  +0.40     commanded MTQ dipole
UP 000:20:34  WDG 00004834     uptime, watchdog kick count
MODE NOMINAL                   status bar: green NOMINAL / red SAFE / yellow STANDBY
```

- **Formatter** — `obsw/ui/dashboard.h` (core library, host unit test
  `ctest -R dashboard`, OBSW-SRS-310..312). No printf, no floats in the
  output, counters saturate.
- **Link statistics** — TMTC calls `obsw_dash_link_note_tc()` per received
  TC frame and `obsw_dash_link_note_tm()` per sent TM; FDIR copies them with
  `obsw_tmtc_get_link_stats()` (critical section, no torn reads).
- **Renderer** — `hal/stm32h7/lcd_dash.c` keeps a shadow of the panel and
  redraws only changed cells (a whole row when its style changes). The first
  render clears GRAM, which holds power-on noise until written.
- **Single owner** — only the FDIR task calls LCD functions once the
  scheduler runs, so no locking is needed. `lcd_console.c` (scrolling text
  console) is still built but no longer used by FDIR.

### Backlight

The backlight on PE10 is **active-low** (LOW = on). `lcd_init()` leaves it
off; `main()` runs `lcd_init()`, clears the screen (GRAM still holds the
previous run's picture), then calls `lcd_backlight_on()`.

Earlier versions drove PE10 HIGH as "on", which turned the backlight off
~0.6 s after reset. That blank screen was blamed on an ST7735R
"power supervisor" resetting the panel, and worked around with a 200 ms
re-wake from FDIR. The panel never actually lost its state; the keepalive
is now `OBSW_LCD_KEEPALIVE` (default OFF), kept only for bench A/B.

All wake paths (`lcd_init`, `lcd_slpout_dispon`, `_yield`, `_fast`) send
the same register table (`s_panel_cfg` in `lcd.c`) and differ only in their
waits.

### UART debug trace

`-DOBSW_UART_ASCII_DEBUG=ON` re-enables the FDIR `B`/`k`/`F` markers and
the `# TM(s,ss)` labels on USART3. They share the UART with binary wire
protocol v3, so the default build leaves them out.

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

## S3 Housekeeping

S3 HK is owned by the **FDIR task**. PUS registers the TC(3,1)/(3,5)/(3,6)
routes at init time (after FDIR) and backfills `s3_ctx.s1` so TC verification
TM is generated, and `s3_ctx.param_table` so TC(3,1) can reference S20
parameters. TC(3,1) runs in a critical section because it appends to the set
table the FDIR task is ticking; 4 free set slots and a 32-entry parameter pool
are reserved for it. The FDIR 1 Hz loop calls `obsw_s3_tick()` — interval units are
seconds on this path.

### HK sets on the FreeRTOS path

| SID | Name | Parameters | Default interval |
|---|---|---|---|
| 2 | `fdir_hk` | `safe_mode_entry_count`, `watchdog_kick_count`, `watchdog_ticks_remaining` | 60 s |

Parameter widths come from the SRDB (`SRDB_HK_<SET>_SIZES`, bound at init with
`obsw_s3_bind_sizes`), so the TM(3,25) layout matches the generated XTCE.
`nominal_hk` (SID 1) is not reported on this target: it carries OBC temperature
and supply voltages, which are not measured yet (INA219, #39).
| 7 | `aocs_bdot_hk` | `bdot_mag_x/y/z` [T], `bdot_m_cmd_x/y/z` [Am²], `bdot_mag_valid` | 10 s |

All sets are **disabled at boot**. Enable via TC(3,5) over CP2102:

```bash
# Enable aocs_bdot_hk (SID 7) every 10 s — TC(3,5) user-data: [set_id=7][interval=10 u32 BE]
# (hex-encode and send via ping_uart.py --raw or a custom script)
```

**Encoding note:** all parameters are big-endian (PUS-C). Floats
(`bdot_mag_x/y/z`, `m_cmd_x/y/z`) are 4-byte IEEE 754; decode on the ground
with `struct.unpack('>f', bytes)`. Before #82 they went out little-endian.

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
| MTQ actuator output | v0.9 | AOCS computes `out.m_cmd` via `bdot.c` and reports it via S3 HK (SID 7); GPIO/PWM driver to physical magnetorquers not yet written |
| ICM-42688 / MPU-6050 gyroscope driver | v0.10 #53 | Hardware not yet owned; `gyro_valid` remains false |
| INA219 power monitor | v0.10 #39 | Not yet implemented |
| STM32H750 Renode socket transport for OpenSVF | v0.11 #54 | Renode script exists; OpenSVF integration not yet validated |
