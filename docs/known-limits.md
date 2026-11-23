# Known limits

What this release does not do, does only on some targets, or has not shown.
Read the Docs keeps one copy of this page per release, so the page of a
release describes that release. The [Targets](targets.md) page gives the
support per target and the evidence for each feature.

OpenOBSW is flight-oriented, not flight-proven: it has not flown, and no
target has run on flight hardware.

## Targets and simulation

- **STM32H7 is not a closed-loop SIL target.** The FreeRTOS image drops every
  sensor frame and sends no actuator frame, and its AOCS reads only the real
  QMC5883L magnetometer. The superloop takes only `sim_time` from a sensor
  frame and runs no AOCS. Over the Renode socket, telecommands work on both
  images and housekeeping on the FreeRTOS image; closed loop needs
  [#54](https://github.com/lipofefeyt/openobsw/issues/54).
- **Thermal input is host-sim only.** `obc_temperature_raw` and
  `thermal_valid` in the sensor frame drive `obc_temperature` in the host and
  aarch64 sims. Every other target ignores them.
- **ZynqMP stops answering in NOMINAL with valid attitude sensors.**
  `renode/test_nominal_aocs_zynqmp.py` passes steps 1 to 3 (STANDBY, then
  SAFE, then NOMINAL by TC(8,1)), then gets no actuator frame in steps 4 and
  5. A ping sent after that gets no reply. Seen on 2026-11-16 and again on
  2026-11-22 ([#147](https://github.com/lipofefeyt/openobsw/issues/147)).
- **MSP430 does not speak wire protocol v4.** It reads and writes
  length-prefixed packets with no type bytes and no end-of-tick marker; see
  [MSP430 framing](icd.md#msp430-framing). Its evidence is one bench run on a
  LaunchPad, with no test script.
- **Frames that are too long desynchronise the stream** on the targets:
  a body over 512 B on STM32H7, over 256 B on ZynqMP. The target does not read
  the body, so it parses the body bytes as new frames. The host sim stops on a
  body over 1024 B ([#152](https://github.com/lipofefeyt/openobsw/issues/152)).
- **ZynqMP cannot send a TM packet over 128 B.** Such a packet would stay at
  the head of the TM store and block all later TM. None of the services
  ZynqMP routes produces one ([#152](https://github.com/lipofefeyt/openobsw/issues/152)).
- **The host sim cannot raise `watchdog_expiry` (0x0010).** It kicks the
  watchdog every tick and never counts it down, so a campaign that waits for
  this event will not see it.
- **Telecommands are accepted on any APID.** Every route uses the wildcard
  APID `0xFFFF`. The SRDB and the tools address every telecommand to `0x010`.
- **The CCSDS transfer frame layers are not used by any target.** The TC
  frame decoder and the TM frame builder are library code with unit tests
  only. SCID, VCID and the frame lengths in
  [Mission configuration](mission-config.md) matter only to them.

## Time

- **Bare-metal loops send TM with a zero time field.** The STM32H7 superloop,
  ZynqMP and MSP430 leave the on-board time in TM at zero (OBSW-SRS-153).
- **The S1 reports of the TC(9,128) that sets the time carry the old time**
  (OBSW-SRS-153).
- **On STM32H7 FreeRTOS, TM time is verified by inspection only.** No test
  checks it on the target.

## STM32H7 FreeRTOS

- **TM of a telecommand arrives after the end-of-tick marker.** The TMTC task
  writes `0xFF` as soon as it has queued the telecommand, and sends the TM
  the PUS task produces later, while it is idle. A reader that stops at
  `0xFF` gets that TM with the next tick. See
  [Behaviour per target](icd.md#behaviour-per-target).
- **A telecommand is dropped without a report when the TC queue is full.** The
  queue holds 4 ([#152](https://github.com/lipofefeyt/openobsw/issues/152)).
- **The TM store has no lock between its writers.** The Mode Manager
  (priority 2) writes its mode events outside a critical section, and FDIR
  (priority 3) writes housekeeping without one, so a priority-3 write can
  interrupt a priority-2 write in the middle. No test has provoked it
  ([#150](https://github.com/lipofefeyt/openobsw/issues/150)).
- **S5 message counters are kept per context.** The PUS task (for S12
  events), FDIR and the Mode Manager each have their own S5 context, so
  TM(5,x) from different tasks can repeat a message count on the same APID
  ([#151](https://github.com/lipofefeyt/openobsw/issues/151)).
- **The S12-to-SAFE locking (#128) is argued, not tested.** The races it closes
  have not been provoked by a test, and it has run in Renode only, not on a
  board.
- **Mode transition events have no target test.** They are tested on the host
  sim (OBSW-SRS-190).

## AOCS

- **B-dot has not closed the loop on hardware**
  ([#52](https://github.com/lipofefeyt/openobsw/issues/52)).
- **The STM32H750 runs on the 64 MHz HSI.** The 480 MHz PLL setup is disabled
  ([#72](https://github.com/lipofefeyt/openobsw/issues/72)).
- **The B-dot high-pass filter is validated on a bench field model only.** The
  run on the opensvf-kde orbital environment is still outstanding
  ([#70](https://github.com/lipofefeyt/openobsw/issues/70)). Until it is done,
  `bdot_gain` (0x20A0) is not retuned.

## SRDB and tools

- **CSV export drops the descriptions of event auxiliary data.** An export and
  re-import loses them, short of OBSW-SRS-224 ("without loss") ([#149](https://github.com/lipofefeyt/openobsw/issues/149)).
- **`pytest test/system srdb/tests` in one run fails at collection**, on a
  conftest name clash. Run the two directories separately ([#153](https://github.com/lipofefeyt/openobsw/issues/153)).

## Testing and evidence

- **The Renode tests run locally only.** The scripts under `renode/` are not in
  CI and are not scanned by `tools/trace.py`; the SRS cites them in Evidence
  lines.
- **Renode runs the STM32H750 at about 0.4 times real time** on a typical
  development machine. Tests that wait a fixed wall-clock time undercount;
  `renode/test_hk_interval_stm32h7.py` measures from TM time instead.
- **`renode/test_ping_zynqmp.py` cannot fail.** It prints the result and
  always exits 0, also when nothing answers ([#148](https://github.com/lipofefeyt/openobsw/issues/148)).
- **OpenSVF's "Renode ZynqMP system test" CI job can pass without testing
  anything.** It skips its steps when it does not find the openobsw ZynqMP
  binary artifact ([#144](https://github.com/lipofefeyt/openobsw/issues/144)).
