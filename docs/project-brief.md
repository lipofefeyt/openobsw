# OpenOBSW + OpenSVF — Project Brief

## What it is

**OpenOBSW** is an open-source, flight-grade On-Board Software stack written in C11. It implements the ECSS PUS-C standard (services S1, S3, S5, S6, S8, S17, S20) with a strict design: zero dynamic allocation, no global state, full HAL isolation via an I/O vtable. The same codebase runs on four targets without modification: a Linux host simulator, STM32H750 (Cortex-M7), MSP430FR5969 (16-bit), and ZynqMP (aarch64).

**OpenSVF** is the companion Space Vehicle Framework — the ground segment and simulation harness. It speaks wire protocol v3 to the OBSW over UART or TCP socket, ingests XTCE telemetry definitions exported from the SRDB, and integrates with YAMCS for mission operations.

## Current state (May 2026)

- Full PUS-C TC/TM stack validated end-to-end (host sim + Renode emulation)
- STM32H750 bare-metal: **flashed and running**, ping TC(17,1) → pong TM(17,2) confirmed via Renode; UART console pending CP2102 adapter arrival
- MSP430FR5969: full stack validated in Renode + real hardware
- ZynqMP: bare-metal build and Renode emulation working
- SRDB: YAML-driven parameter/TC/event database with XTCE 1.2 export for YAMCS
- AOCS: B-dot (safe mode, magnetorquers) and PD quaternion (nominal mode, reaction wheels) — tested in simulation

## Hardware in play

| Item | Status | Role |
|---|---|---|
| WeAct STM32H750VBT6 | Owned, flashed | Primary OBC target |
| ST-Link V2 | Owned | SWD debug/flash |
| Raspberry Pi | Owned | Future opensvf ground station |
| CP2102 USB-UART | Ordered | STM32H750 console |
| QMC5883L magnetometer | Wanted (AliExpress) | First real sensor — closes B-dot loop on HW |

## Near-term priorities

1. CP2102 arrives → verify boot banner and live TC/TM on real hardware
2. QMC5883L arrives → write STM32H750 I2C HAL driver, wire real B-dot loop
3. opensvf ground segment → move to Raspberry Pi, connect to STM32H750 over UART
4. YAMCS integration → live TM display from real hardware via XTCE definitions

## Why it matters

Most open-source OBSW projects are either toy demos or stripped-down copies of proprietary stacks. OpenOBSW is designed to the same constraints as flight software — deterministic memory, standard protocols, hardware portability — but fully open, with a companion ground segment. The goal is a complete, documented, end-to-end spacecraft software stack that a small team or university mission can actually fly.
