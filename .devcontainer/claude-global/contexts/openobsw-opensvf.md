# OpenOBSW — OpenSVF Integration Context

## What this repo is

**openobsw** is a flight-grade On-Board Software stack (C11, PUS-C) that runs unchanged on four targets: Linux host simulator, STM32H750 (Cortex-M7), MSP430FR5969 (16-bit bare-metal), and ZynqMP (aarch64). Zero dynamic allocation, no global state, full HAL isolation via an I/O vtable.

## Current state (May 2026)

- Full PUS-C TC/TM stack validated end-to-end on host sim and Renode
- STM32H750 bare-metal: flashed and running, TC(17,1)→TM(17,2) confirmed in Renode; UART console pending CP2102 adapter
- MSP430FR5969: full stack validated in Renode and real hardware
- ZynqMP: bare-metal build and Renode emulation working
- SRDB: YAML-driven parameter/TC/event database with XTCE 1.2 export at `build/xtce/mission.xtce`
- AOCS: B-dot (SAFE, magnetorquers) and PD quaternion (NOMINAL, reaction wheels)

---

## Wire protocol v3

Framed pipe over stdin/stdout (host sim) or TCP socket (Renode, port 3456):

| Byte | Direction | Payload |
|---|---|---|
| `0x01` | → OBSW | `uint16 BE len` + TC transfer frame |
| `0x02` | → OBSW | `uint16 BE len` + `obsw_sensor_frame_t` (sensor injection) |
| `0x03` | ← OBSW | `uint16 BE len` + `obsw_actuator_frame_t` |
| `0x04` | ← OBSW | `uint16 BE len` + TM packet bytes |
| `0xFF` | ← OBSW | end-of-tick (no length field) |

### obsw_sensor_frame_t (47 B, packed, LE)

```c
float   mag_x, mag_y, mag_z;              // [T]
uint8_t mag_valid;
float   st_q_w, st_q_x, st_q_y, st_q_z;  // star tracker quaternion
uint8_t st_valid;
float   gyro_x, gyro_y, gyro_z;           // [rad/s]
uint8_t gyro_valid;
float   sim_time;                          // [s]
```

### obsw_actuator_frame_t (29 B, packed, LE)

```c
float   mtq_dipole_x, mtq_dipole_y, mtq_dipole_z;  // [Am²]
float   rw_torque_x,  rw_torque_y,  rw_torque_z;   // [Nm]
uint8_t controller;   // 0 = b-dot, 1 = ADCS PD
float   sim_time;
```

Framing code: `contrib/svf_protocol/` — keep in sync with `sim/main.c` when changing protocol bytes or struct layouts.

---

## Spacecraft identity

| Field | Value |
|---|---|
| Name | Demo Satellite |
| SCID | 0x001 (placeholder — mission-assigned) |
| TC APID | 0x010 |
| TM source APID | 0x103 |

---

## PUS-C services implemented

| Service | Subservices |
|---|---|
| S1 TC Verification | TM(1,1) accept, TM(1,2) fail, TM(1,7) complete, TM(1,8) fail |
| S3 Housekeeping | TC(3,1) define, TC(3,5/6) enable/disable, TM(3,25) report |
| S5 Event Reporting | TM(5,1–4); HIGH severity → FSM to SAFE |
| S6 Memory Management | TC(6,2) load, TC(6,5) CRC check, TC(6,9) dump |
| S8 Function Management | TC(8,1) perform; function ID 1 = recover to NOMINAL |
| S17 Are-You-Alive | TC(17,1) ping → TM(17,2) pong |
| S20 Parameter Management | TC(20,1) set, TC(20,3) get, TM(20,2) report |

---

## Telecommands

| TC | APID | Description |
|---|---|---|
| TC(17,1) | 0x010 | Are-you-alive ping |
| TC(3,5) | 0x010 | Enable HK report — args: `set_id` (u8), `interval_ticks` (u32) |
| TC(3,6) | 0x010 | Disable HK report — args: `set_id` (u8) |
| TC(8,1) | 0x010 | Perform function — args: `function_id` (u16), `args_len` (u8) |

---

## Housekeeping sets (S3)

| SID | Name | Parameters |
|---|---|---|
| 1 | NOMINAL_HK | OBC_TEMPERATURE, OBC_VOLTAGE_3V3, OBC_VOLTAGE_5V, OBC_UPTIME |
| 2 | FDIR_HK | SAFE_MODE_ENTRY_COUNT, WATCHDOG_KICK_COUNT, WATCHDOG_TICKS_REMAINING |
| 3 | DHS_OBC_HK | OBC_MODE, OBC_OBT, OBC_WATCHDOG_STATUS, OBC_MEMORY_USED_PCT, OBC_HEALTH, OBC_RESET_COUNT, OBC_CPU_LOAD |

---

## S5 Events

| ID | Name | Severity | Safe trigger |
|---|---|---|---|
| 0x0001 | boot_complete | INFO | no |
| 0x0002 | safe_mode_entry | HIGH | no |
| 0x0003 | safe_mode_exit | INFO | no |
| 0x0010 | watchdog_expiry | HIGH | yes |
| 0x0020 | power_fault_3v3 | HIGH | yes |
| 0x0021 | power_fault_5v | HIGH | yes |
| 0x0030 | comms_timeout | MEDIUM | no |
| 0x0040 | hk_store_full | LOW | no |
| 0x0050 | temperature_soft_limit | LOW | no |
| 0x0051 | temperature_hard_limit | HIGH | yes |

---

## AOCS

| Mode | Algorithm | Sensors | Actuators |
|---|---|---|---|
| SAFE | B-dot (`aocs/bdot.c`) | Magnetometer only | Magnetorquers |
| NOMINAL | PD quaternion (`aocs/adcs.c`) | Star tracker + gyro | Reaction wheels |

Control loop runs at 10 Hz on STM32H7 (FreeRTOS AOCS task); same logic drives the host sim tick.

---

## Equipment hardware profiles

Profiles live in `srdb/data/hardware/`:

| Profile ID | Type | Key params |
|---|---|---|
| `mag_default` | magnetometer | noise 1e-7 T, 10 Hz |
| `gyro_default` | gyroscope | ARW 1e-4 rad/s/√Hz, 100 Hz |
| `rw_default` | reaction_wheel | 6000 rpm, 0.2 Nm max |
| `mtq_default` | magnetorquer | 10 Am², xyz axes |

---

## XTCE export

Generated at build time: `build/xtce/mission.xtce` (XTCE 1.2, ready for YAMCS).

Rebuild with:
```bash
source .venv/bin/activate
cmake --build build
```

---

## Transport options for OpenSVF

| Mode | Config | When |
|---|---|---|
| `pipe` | `binary: ./build/sim/obsw_sim` | Development, CI |
| `socket` | `host: localhost`, `port: 3456` | Renode SIL (ZynqMP or STM32H7) |

Renode scripts: `renode/zynqmp_obsw.resc`, `renode/stm32h750_obsw.resc`.
