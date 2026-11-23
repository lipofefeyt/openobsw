# FreeRTOS on STM32H750

## Overview

The STM32H750 FreeRTOS image (`-DOBSW_FREERTOS=ON`) runs the stack as five
statically allocated tasks. The superloop image (the default) and the other
targets are single loops; see [Architecture](architecture.md).

**Design constraints:**

- Zero dynamic allocation: every FreeRTOS object uses a `*Static()` variant
  (`configSUPPORT_STATIC_ALLOCATION=1`, `configSUPPORT_DYNAMIC_ALLOCATION=0`).
- Contexts are passed explicitly; shared state is listed under
  [Inter-task communication](#inter-task-communication).
- All I/O goes through `obsw_io_ops_t`.

---

## Task architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                           STM32H750                              │
│                                                                  │
│  ┌──────────────┐   TC queue (4)   ┌──────────────┐              │
│  │  TMTC        │ ───────────────► │  PUS         │              │
│  │  prio 4      │ ◄─────────────── │  prio 3      │              │
│  │  USART3      │   task notify    │  dispatcher, │              │
│  └──────▲───────┘                  │  S11, S19    │              │
│         │ TM store                 └──────┬───────┘              │
│         │ (PUS, FDIR, Mode Manager write) │ mode requests        │
│  ┌──────┴───────┐                  ┌──────▼───────┐              │
│  │  FDIR        │  S12 → SAFE      │ Mode Manager │              │
│  │  prio 3      │ ───────────────► │ prio 2       │              │
│  │  IWDG, S12,  │                  │ FSM owner,   │              │
│  │  S3, LCD     │                  │ LEOP timer   │              │
│  └──────────────┘                  └──────┬───────┘              │
│                                           │ mode                 │
│                                    ┌──────▼───────┐              │
│                                    │ AOCS         │              │
│                                    │ prio 2,10 Hz │              │
│                                    └──────────────┘              │
└──────────────────────────────────────────────────────────────────┘
```

---

## Task definitions

### TMTC task: `task/tmtc.c`

| Property | Value |
|---|---|
| Priority | 4 (highest user task) |
| Trigger | USART3 RXNE interrupt; woken by the PUS task notify |
| Stack | Static |

Owns the UART. When no byte is waiting it re-arms the RXNE interrupt and
sleeps for up to 1 ms (`ulTaskNotifyTake`); the first byte of a frame, or a
notify from the PUS task, wakes it.

**Uplink:** reads wire protocol v4 frames (`[type][uint16 BE len][body]`).
A TC frame (type `0x01`, body up to 512 B) goes on the TC queue for the PUS
task as an `obsw_tc_frame_item_t`. The queue holds 4; when it is full the TC
is dropped, with no report. Every other frame type is read and dropped,
sensor frames included.

**Downlink:** drains the TM store as `[0x04][len_hi][len_lo][packet]`, after
each inbound frame and whenever it wakes. It writes one `0xFF` per inbound
frame, also for a dropped frame and a bad length.

> **Timing:** `0xFF` is sent as soon as the TC is queued, before the PUS task
> has run it. The TM of that TC is sent later, while TMTC is idle, so a
> reader that stops at `0xFF` gets it with the next tick. See
> [Behaviour per target](icd.md#behaviour-per-target).

**UART RX pull-up:** `uart_init()` enables the internal pull-up on PD9 (RX).
Without it a floating RX line generates continuous false RXNE events; TMTC
spins at priority 4, starves FDIR, the IWDG is not kicked and the board
resets every 4 s.

**`taskYIELD()` after each frame** (valid or not) gives FDIR and the
lower-priority tasks a slot even under continuous UART traffic.

---

### PUS task: `task/pus.c`

| Property | Value |
|---|---|
| Priority | 3 |
| Trigger | TC queue, with a timeout so due S11 and S19 TCs run without uplink |
| Stack | Static |

Feeds each queued TC to the dispatcher, which routes S3, S6, S8, S9, S11,
S12, S17, S19 and S20 (see [Targets](targets.md#routed-telecommands)). The
dispatcher applies the mode gate before route lookup:

```c
obsw_tc_dispatcher_init(&d, routes, n, obsw_s1_dispatch_responder, &s1_ctx);
obsw_tc_dispatcher_set_gate(&d, obsw_fsm_tc_gate, obsw_mode_get_fsm());
```

A TC refused by the gate in SAFE or STANDBY gets TM(1,2) code `0x0007`; an
unrouted TC gets `0x0002` (unknown service) or `0x0003` (unknown
subservice). Any well-formed TC, accepted or rejected, counts as ground
contact for the LEOP timer. S8 function 1 (`OBSW_S8_FN_RECOVER_NOMINAL`)
calls `obsw_mode_request_nominal()`; the Mode Manager applies it.

After each wake the task runs due S11 activities and the S19 actions that
FDIR queued, then notifies TMTC to send the TM.

---

### Mode Manager task: `task/mode.c`

| Property | Value |
|---|---|
| Priority | 2 |
| Trigger | Periodic, `vTaskDelayUntil()` at 1 Hz |
| Stack | Static |

Owns the `obsw_fsm_ctx_t`. Each second, inside a critical section, it
applies the pending mode request (SAFE, STANDBY or NOMINAL) and runs the
LEOP timer (STANDBY to SAFE after `standby_safe_timeout` without ground
contact). Outside the critical section it reports the mode event of a
transition: `SRDB_EVENT_SAFE_MODE_ENTRY` (HIGH), `SRDB_EVENT_STANDBY_ENTRY`
or `SRDB_EVENT_NOMINAL_ENTRY` (INFO), without auxiliary data
(OBSW-SRS-190).

---

### AOCS task: `task/aocs.c`

| Property | Value |
|---|---|
| Priority | 2 |
| Trigger | Periodic, `vTaskDelayUntil()` at 10 Hz |
| Stack | Static |

Reads the mode each tick and the S20 gains (`bdot_gain`, `bdot_hpf_tau`,
`adcs_kp`, `adcs_kd`) with `obsw_pus_s20_get_float()`:

| Mode and sensors | Control law | Output |
|---|---|---|
| STANDBY | off | none |
| SAFE, `mag_valid` | B-dot (`bdot.c`) | MTQ dipole command (reported in HK, not driven) |
| NOMINAL, `st_valid && gyro_valid` | PD quaternion (`adcs.c`) | RW torque command (not driven) |
| anything else | idle | last command held |

There is no star tracker or gyro on this board, so `st_valid` and
`gyro_valid` are always false and NOMINAL never runs PD control. B-dot state
is reset on each NOMINAL to SAFE transition. Configuration: `gain = 1e4`,
`max_dipole = 10 A m^2`, `hpf_tau = 15 s`, `kp = 0.5`, `kd = 0.1`,
`max_torque = 0.01 N m`, `dt = 0.1 s`.

**QMC5883L magnetometer** on I2C1 (PB8/PB9, 100 kHz). `mag_valid` is true
when the chip ID checks out and each read returns data; under Renode
(`OBSW_RENODE=ON`) it is always false. After each tick the field `b[3]` and
the dipole command go into `obsw_aocs_hk_t` (`include/obsw/task/aocs.h`) for
S3 housekeeping.

The AOCS task does not read sensor frames: the image is not a closed-loop
SIL target yet (#54). Magnetorquer and reaction wheel drivers are not
written.

---

### FDIR task: `task/fdir.c`

| Property | Value |
|---|---|
| Priority | 3 |
| Trigger | Periodic, `vTaskDelayUntil()`: 200 ms sub-tick, full tick every 5th (1 s) |
| Stack | Static |

Owns the hardware IWDG and the S3 housekeeping context. It initialises
before PUS, which gets the S3 context through `obsw_fdir_get_s3_ctx()`.

Every 200 ms sub-tick: **kick the IWDG** (`IWDG_KR = 0xAAAA`; 4 s timeout,
prescaler /128 from LSI ≈ 32 kHz, reload 1000).

Every 1 s full tick:

1. **Advance on-board time** (`obsw_pus_obt_advance(1000)`).
2. **S12 limit checks** (`obsw_pus_s12_check()`): raises the limit events
   and, for an event on the safe-trigger list, commands SAFE inside a
   critical section; S19 actions bound to the event are queued for the PUS
   task.
3. **Heartbeat LED** (PE3) and the watchdog kick count for HK.
4. **Boot event**: on the first full tick, `SRDB_EVENT_BOOT_COMPLETE` (INFO).
5. **SAFE entry count** for HK, from the mode read each tick.
6. **S3 tick** (`obsw_s3_tick()`): enabled sets emit TM(3,25) when their
   countdown reaches zero. Intervals are in seconds on this target.
7. **LCD dashboard**: see [LCD dashboard](#lcd-dashboard).

A stuck FDIR task stops the IWDG kick and resets the board within 4 s.

The SAFE and STANDBY TC whitelist is generated from `safe_allowed: true` in
`srdb/data/telecommands.yaml` as `SRDB_SAFE_TC_WHITELIST` and shared by
every target; see [Telecommands](srdb/telecommands.md).

---

## Inter-task communication

| Channel | Type | Producer | Consumer | Notes |
|---|---|---|---|---|
| TC queue | `QueueHandle_t`, static, 4 slots | TMTC | PUS | `obsw_tc_frame_item_t`: raw packet and 16-bit length. A TC is dropped when the queue is full |
| TM store | `obsw_tm_store_t` ring, 32 slots | PUS (prio 3), FDIR (prio 3), Mode Manager (prio 2) | TMTC (prio 4) | `volatile` head and tail, no lock. The two priority-3 writers do not preempt each other, but either can preempt the Mode Manager in the middle of an enqueue; see [Known limits](known-limits.md#stm32h7-freertos) |
| AOCS HK | `obsw_aocs_hk_t`, volatile struct | AOCS | FDIR (S3) | Each field is one 32-bit write, atomic on Cortex-M7; `mag_valid` is written last |
| Task notify | `xTaskNotify` | PUS | TMTC | Immediate TM drain after a TC or an S11/S19 run |
| Mode FSM | `obsw_fsm_ctx_t` in `mode.c` | Mode Manager (requests, LEOP timer); FDIR through S12 (safe-trigger event to SAFE) | PUS (gate), AOCS, FDIR | Two writers. Every transition is made inside a critical section: the Mode Manager around its request and timer block, FDIR inside `obsw_pus_s12_check()`. Readers take single-word reads of `mode`. A SAFE entry made by S12 is also filed with `obsw_mode_request_safe()`, which discards a recover-to-NOMINAL request pending from before the fault |

---

## Priority table

| Task | Priority | Rationale |
|---|---|---|
| TMTC | 4, highest | Must not drop TC frames or stall TM |
| FDIR | 3 | IWDG kick and fault response every cycle |
| PUS | 3 | TC processing completes promptly |
| Mode Manager | 2 | Mode transitions once per second |
| AOCS | 2 | Fixed-rate control; can be preempted |
| Idle | 0 | FreeRTOS idle task |

---

## LCD dashboard

The ST7735R driver (`hal/stm32h7/lcd.c`) drives the 160×80 panel (26×10
cells of 6×8 px, 5×7 font). FDIR shows a fixed-layout dashboard, refreshed
once per second. The title shows the release version (`OBSW_VERSION_STRING`):

```
openobsw v0.20.0 STM32H750     title bar (cyan)
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

- **Formatter:** `obsw/ui/dashboard.h` (core library, host unit test
  `ctest -R dashboard`, OBSW-SRS-310..312). No printf, no floats in the
  output, counters saturate.
- **Link statistics:** TMTC calls `obsw_dash_link_note_tc()` per received TC
  frame and `obsw_dash_link_note_tm()` per sent TM; FDIR copies them with
  `obsw_tmtc_get_link_stats()` (critical section, no torn reads).
- **Renderer:** `hal/stm32h7/lcd_dash.c` keeps a shadow of the panel and
  redraws only changed cells (a whole row when its style changes); every
  30 s it redraws the whole grid. The first render clears GRAM, which holds
  power-on noise until written.
- **Single owner:** only FDIR calls LCD functions once the scheduler runs, so
  no locking is needed. `lcd_console.c` (scrolling text console) is still
  built but no longer used.

### Backlight

The backlight on PE10 is **active-low** (LOW = on). `lcd_init()` leaves it
off; `main()` runs `lcd_init()`, clears the screen (GRAM still holds the
previous run's picture), then calls `lcd_backlight_on()`.

Earlier versions drove PE10 HIGH as "on", which turned the backlight off
about 0.6 s after reset. That blank screen was blamed on an ST7735R "power
supervisor" resetting the panel and worked around with a 200 ms re-wake from
FDIR. The panel never lost its state; the keepalive is now
`OBSW_LCD_KEEPALIVE` (default OFF), kept only for bench A/B tests.

All wake paths (`lcd_init`, `lcd_slpout_dispon`, `_yield`, `_fast`) send the
same register table (`s_panel_cfg` in `lcd.c`) and differ only in their
waits.

### UART debug trace

`-DOBSW_UART_ASCII_DEBUG=ON` re-enables the FDIR `B`/`k`/`F` markers and the
`# TM(s,ss)` labels on USART3. They share the UART with the binary wire
protocol, so the default build leaves them out.

---

## Fault handlers

`platform/stm32h7/freertos_hooks.c` overrides the weak default handlers:

| Handler | Behaviour |
|---|---|
| `HardFault_Handler` | Naked trampoline: captures MSP/PSP, calls `hard_fault_handler_c()`, which prints `PC`, `LR`, `HFSR`, `CFSR`, `MMAR`, `BFAR` to the UART, then spins (the IWDG resets after 4 s) |
| `vApplicationStackOverflowHook` | Prints the task name to the UART, then spins |
| `obsw_freertos_assert_fail` | Prints file and line to the UART, then spins |

All three write directly to the USART3 TDR (no driver dependency), so they
work whatever the scheduler state.

> **Earlier bug:** `obsw_freertos_assert_fail` used to execute `bkpt #0`.
> Without a debugger attached, `bkpt` raises a HardFault, and the old
> `HardFault_Handler` was `b .` (spin). Any FreeRTOS assert led to a silent
> reset loop: bkpt, HardFault, spin, FDIR starved, IWDG reset.

---

## S3 housekeeping

S3 is owned by the FDIR task. PUS routes TC(3,1), TC(3,3), TC(3,5) and
TC(3,6) to the FDIR S3 context at init (after FDIR), and fills in
`s3_ctx.s1` so TC verification TM is generated and `s3_ctx.param_table` so
TC(3,1) can reference S20 parameters. TC(3,1) and TC(3,3) run in a critical
section because they change the set table FDIR is ticking; 4 free set slots
and a 32-entry parameter pool are reserved for them.

### Boot sets on this target

| SID | Name | Default interval |
|---|---|---|
| 2 | `fdir_hk` | 60 s |
| 7 | `aocs_bdot_hk` | 10 s |

Their parameters and layout are on the [Housekeeping sets](srdb/hk-sets.md)
page. Parameter widths come from the SRDB (`SRDB_HK_<SET>_SIZES`, bound at
init with `obsw_s3_bind_sizes`), so the TM(3,25) layout matches the
generated XTCE. `nominal_hk` (SID 1) is not reported on this target: it
carries OBC temperature and supply voltages, which are not measured yet
(INA219, #39).

Both sets are **disabled at boot**; enable them with TC(3,5)
(`[set_id][interval_ticks u32 BE]`). `renode/test_hk_interval_stm32h7.py`
does this in Renode.

All parameters are big-endian (PUS-C). Floats (`bdot_mag_x/y/z`,
`bdot_m_cmd_x/y/z`) are 4-byte IEEE 754; decode them with
`struct.unpack('>f', bytes)`.

---

## Ground tools

| Tool | Path | Purpose |
|---|---|---|
| `ping_uart.py` | `tools/ping_uart.py` | Send TC(17,1) over the CP2102 UART; decode TM(1,1), TM(17,2), TM(1,7) |
| `send_ping.py` | `sim/send_ping.py` | Send TC(17,1) to the host sim |

```bash
# Requires: pip install pyserial
python3 tools/ping_uart.py /dev/ttyUSB0          # 3 pings, parsed TM
python3 tools/ping_uart.py /dev/ttyUSB0 --raw    # raw hex dump
```

The dispatcher receives a PUS-C TC **space packet**, not a TC transfer
frame; the wire protocol carries space packets directly. A TC(17,1) to APID
`0x010` is 11 bytes:

```
18 10  C0 00  00 04  11  11  01  00 00
  │       │      │   │   │   │    └─ source ID
  │       │      │   │   │   └─ subservice = 1
  │       │      │   │   └─ service = 17
  │       │      │   └─ PUS-C secondary header (version + ack flags)
  │       │      └─ data length = 4 (payload 5 bytes)
  │       └─ sequence: standalone, count 0
  └─ packet ID: TC, secondary header flag, APID 0x010
```

---

## Static allocation

All FreeRTOS objects use `*Static()` variants. `configASSERT()` is enabled in
all builds. Stack overflow detection (`configCHECK_FOR_STACK_OVERFLOW=2`)
reports through `platform/stm32h7/freertos_hooks.c`.

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

CI builds this image on every PR. The superloop (`OBSW_FREERTOS` OFF) stays
buildable for Renode smoke tests.

FreeRTOS V11.1.0 is fetched with `FetchContent` at configure time, port
`GCC_ARM_CM7`. `libm` (`acosf`, `sqrtf`) needs a bare-metal `__errno()` stub
(`platform/stm32h7/errno_stub.c`) because newlib-nano does not provide it.

---

## Not yet wired

| Item | Issue | Notes |
|---|---|---|
| Sensor frames and actuator frames (closed-loop SIL over Renode) | #54 | TMTC drops sensor frames; no actuator frame is sent |
| MTQ actuator output | #52 | AOCS computes the dipole and reports it in SID 7; no driver to physical magnetorquers |
| ICM-42688 gyroscope driver | #53 | `gyro_valid` stays false |
| INA219 power monitor | #39 | Supply voltages are not measured |
