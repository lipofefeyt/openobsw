# Changelog

All notable changes to openobsw. Versions follow the release trains in the
README roadmap. Releases before v0.16 are described on the
[GitHub releases page](https://github.com/lipofefeyt/openobsw/releases).

## [v0.20] - Documentation

The documentation release: a versioned site on Read the Docs,
[openobsw.readthedocs.io](https://openobsw.readthedocs.io/), whose reference
pages are generated from the sources and checked in CI. No change to the
flight code, the wire protocol (still v4) or the SRDB content; opensvf
needs no change to move its pin to this tag.

### Added

- **Documentation site** (#135): mkdocs-material on Read the Docs, one
  version per release tag from v0.20, plus `latest` for `main`. Sections:
  Getting started, Concepts, Reference, Known limits, Releases. Toolchain
  pinned in `docs/requirements.txt` (mkdocs 1.6.1, Material 9.8.2), no
  mkdocs plugins.
- **Generated reference pages** (#136), written by `tools/gen_docs.py`
  before each build:
  - Wire protocol (ICD) from `sim/sensor_inject.h`, including the behaviour
    of each target (reply order, limits, what it ignores) and the MSP430's
    own framing;
  - SRDB parameters, events, telecommands and HK sets from `srdb/data/`;
  - requirements traceability from `docs/SRS.md` and the tests;
  - Targets: support tier per target, features with the kind of evidence
    (CI, local Renode, bench, none) and the test that shows each, and the
    routed TCs, from the hand-kept `docs/targets.yaml`.
- **Known limits page** (#138): what this release does not do or has not
  shown, per target, with its issue.
- **CI checks** (`docs` job, on every PR): strict mkdocs build, external link
  check (lychee), and `gen_docs.py`, which fails when
  `contrib/svf_protocol/obsw_svf_protocol.h`, `renode/proto.py` or
  `sim/wire_proto.py` disagree with `sim/sensor_inject.h`, when a target's
  TC list in `docs/targets.yaml` differs from its route table, or when a
  test it cites does not exist.
- **CI jobs** `aarch64-sim` (cross-build, ctest and `test/system` under
  `qemu-aarch64`) and `build-stm32h7-freertos` (the FreeRTOS image; the
  existing STM32H7 job builds the superloop).
- Renode test `renode/test_hk_interval_stm32h7.py`: S3 housekeeping
  intervals on the STM32H7 FreeRTOS build, measured from TM time (#51).
- SRDB test pinning TC APID `0x010` and TM source APID `0x103` (#140).

### Changed

- `srdb/data/spacecraft.yaml`: `apid_default` (0x103) is documented as the
  TM source APID, not a TC address; every TC stays on `0x010`, and routes
  keep the `0xFFFF` wildcard (#140).
- README, `architecture.md`, `freertos-architecture.md` and
  `integration-guide.md`: stale facts corrected (layer model without
  transfer frames, five FreeRTOS tasks, FDIR timing, B-dot high-pass
  filter, STM32H7 clock, MSP430 UART, targets by tier); OpenSVF setup now
  links OpenSVF's own docs.
- `sim/sensor_inject.h`: per-field comments with units (comments only).
- SRDB descriptions: em-dashes replaced (text only).
- `cmake/aarch64-linux-gnu.cmake` also finds `aarch64-linux-gnu-gcc`.
- Roadmap: v0.20 is Documentation; the Hardware loop train moves to v0.21.

### Fixed

- `renode/test_ping_stm32h7.py` parses through a leading end-of-tick byte
  and fails when TM(1,1), TM(17,2) or TM(1,7) is missing; it passed with 0
  packets before (#114). TCs confirmed on both STM32H7 images in Renode
  (#106).
- Comments in `tc/dispatcher.h` (input is a space packet) and
  `hal/stm32h7/uart.c` (USART3 on HSI 64 MHz, BRR 556).

### Known limits

See the [Known limits](https://openobsw.readthedocs.io/en/v0.20/known-limits/)
page of this release. New issues from the documentation work: #147 to #153.

## [v0.19] — Recovery evidence

### Added

- **Wire protocol v4** (#122): the sensor frame gains an OBC thermal input,
  `int16 obc_temperature_raw` (SRDB raw counts) and `uint8 thermal_valid`,
  after `sim_time` (47 → 50 bytes). When valid, the host sim writes it into
  `obc_temperature` (S20 `0x0001`) before the tick's S12 check and HK, so a
  simulator can raise and clear a thermal anomaly in any mode, SAFE included,
  where TC(20,1) is gated (OBSW-SRS-246). The host sim exits with status 2 on
  a sensor frame of any other length, naming a v3 sender (OBSW-SRS-245);
  ZynqMP and the STM32H7 superloop ignore such frames. All banners print the
  protocol version (`[OBSW] Host sim started (wire protocol v4).`).
  **Breaking for SVF senders:** opensvf must send the 50-byte frame (opensvf
  #194) before it can pin this release.
- Host sim reports mode transitions as S5 events, like the FreeRTOS Mode
  Manager: `safe_mode_entry` (`0x0002`, HIGH), `standby_entry` (`0x0004`) and
  `nominal_entry` (`0x0005`), with the TC or in the tick that changed the
  mode (OBSW-SRS-190). SRDB: event `0x0003` `safe_mode_exit`, which no target
  ever emitted, is removed, and `safe_mode_entry` no longer declares a
  `trigger_source` auxiliary byte that no target sent.
- STM32H750 LCD dashboard: fixed 26×10 layout with mode, OBT, reset cause,
  TC/TM link counters, last TM and event, AOCS law, magnetometer field and
  commanded dipole, uptime and watchdog count. The core formatter
  (`obsw/ui/dashboard.h`, OBSW-SRS-310..313) is host unit-tested; the LCD
  renderer redraws only changed cells.
- LEOP autonomy timer as a core module (`fdir/standby_timer.h`,
  OBSW-SRS-187..189): STANDBY → SAFE after `standby_safe_timeout` seconds
  without ground contact (new SRDB parameter `0x0008`, S20-tunable on
  STM32H7, default 300 s, `0` = immediately). Any well-formed TC restarts
  the countdown, which the old hard-coded 300 s timer never did. STM32H7
  build option `OBSW_STANDBY_SAFE_TIMEOUT_S` sets the initial value (#111).
- STM32H7 build options `OBSW_UART_ASCII_DEBUG` and `OBSW_LCD_KEEPALIVE`
  (both default OFF).
- STM32H7 FreeRTOS: S12 monitoring now commands SAFE, as on the host sim. A
  HIGH monitoring event on the SRDB safe-trigger list moves the Mode
  Manager's FSM to SAFE from the FDIR tick (OBSW-SRS-295). The Mode Manager
  makes its own transitions inside a critical section so the two writers
  cannot interleave, and a recover-to-NOMINAL request pending from before
  the fault is discarded. `renode/test_s12_safe_stm32h7.py` (`renode-test-s12-safe`).

### Changed

- S20 tables take each parameter's type from the generated
  `SRDB_PARAM_<NAME>_S20_TYPE`. `obc_temperature` (`int16` in the SRDB) is
  now signed in the host sim, so S12 limits work on negative raw values; it
  was compared as unsigned before.
- STM32H7 default build keeps USART3 binary-only: FDIR debug markers and
  `# TM(s,ss)` labels are behind `OBSW_UART_ASCII_DEBUG`.
- ST7735R wake sequence is one register table shared by all wake paths.
- TMTC's 8-entry TM log replaced by saturating link statistics.
- Dev setup: the devcontainer and native WSL2 no longer fight over one
  Python venv. `scripts/env.sh` selects `.venv` (devcontainer, plain Linux)
  or `.venv-wsl` (WSL2); `host-build` refuses a `build/` configured from the
  other environment instead of looping.

### Fixed

- The 4-byte time field of every TM packet was always zero: each service
  context has a `timestamp` the caller is meant to keep current, and no
  target did. The host sim and the STM32H7 FreeRTOS tasks now stamp it with
  OBT seconds before they report (OBSW-SRS-153), so events and verification
  reports carry on-board time, not only HK set 3's `obc_obt`.
- STM32H7 FreeRTOS FDIR hard-coded three safe-trigger events and missed
  `watchdog_expiry` (`0x0010`), which the SRDB also marks. All targets now
  load the generated `SRDB_SAFE_TRIGGER_IDS` through the new
  `obsw_s5_set_safe_triggers()`, which never writes past the list; the build
  fails if the SRDB list outgrows `OBSW_S5_MAX_SAFE_TRIGGERS`.
- STM32H750 LCD went black ~0.5 s after every boot: the backlight on PE10
  is active-low and the driver drove it HIGH as "on". This was previously
  attributed to an ST7735R power-supervisor reset; the 200 ms FDIR re-wake
  that worked around it is now `OBSW_LCD_KEEPALIVE`, default OFF. `main()`
  clears the stale picture before turning the backlight on.
- STM32H7 reset cause used the wrong RCC_RSR bit positions (off by one or
  two), so a power-on read as "Software", the NRST button as "BOR" and a
  software reset as "IWDG". Now uses the RM0433 positions, verified on the
  bench (NRST button reads 0x00460000 = PINRSTF | CPURSTF).
- LCD title and boot banners showed the SRDB data version (0.1.0) as the
  openobsw version; the software version (0.7.0 in `obsw.h` and CMake) was
  stale too. `include/obsw/version.h` is now the single source (0.18.0),
  CMake reads `project(VERSION)` from it, and the banners print both (#117).
- LCD showed stale lines: the console was re-initialised every second
  without clearing the screen.
- SRDB limit events (`0x0020`, `0x0021`, `0x0050`, `0x0051`) declared `uint16`
  or `int16` auxiliary data, but they are raised by S12, which sends the
  monitored value as a big-endian float32. They now declare `value: float32`;
  the generated header gains `SRDB_EVENT_<NAME>_AUX_LEN` and the build checks
  it against `OBSW_S12_EVENT_AUX_LEN`.

## [v0.18] — Service parity with OpenSVF

### Added

- TC(3,3) delete housekeeping sets created by TC(3,1), all or nothing;
  freed slots and parameter storage are reused (#91).
- SRDB repeated TC arguments: a last argument with `count_from` repeats N
  times and is exported to XTCE as an `ArrayArgumentType` sized by the
  count argument. TC(3,1) and TC(3,3) are now in `telecommands.yaml` (#92).
- PUS S11 time-based scheduling: TC(11,4) inserts a CUC-4,2-tagged TC,
  released at OBT through the normal dispatcher (SAFE gate and S1 reports
  apply at release); TC(11,5)/(11,6) delete, TC(11,17)/(11,18)
  enable/disable. `obsw_tc_packet_check()` validates stored TCs for S11
  and S19.
- PUS S12 on-board monitoring: TC(12,1)–(12,5), limit checks on S20
  parameters each control tick, S5 events on entering an out-of-limit
  state, latched until back in range. Routed on the host sim and STM32H7
  FreeRTOS; wire format matches opensvf's `PusService12`.
- PUS S19 event-action: TC(19,1)–(19,5) link an S5 event to a stored TC,
  dispatched on the next tick through the normal dispatcher (SAFE gate and
  S1 reports apply). S5 gains an optional per-event hook.
- S20 parameters carry their type (`OBSW_S20_U32`, `_I32`, `_F32`);
  `obsw_s20_find()` and `obsw_s20_as_float()` accessors.

### Changed

- MISRA C:2012 Advisory rules 12.1 (explicit precedence) and 13.3
  (increment side effects) are now enforced at zero in the core; 69 sites
  rewritten with no behaviour change.

### Fixed

- Boot-time HK sets did not match their SRDB layout, so the generated XTCE
  mis-decoded them: `nominal_hk` (SID 1) and `fdir_hk` (SID 2) on the host
  sim and MSP430, and the first `fdir_hk` field on STM32H7 FreeRTOS (sent as
  4 bytes, defined as 2). Widths now come from the SRDB
  (`SRDB_HK_<SET>_SIZES`, `obsw_s3_bind_sizes()`), counts are checked at
  compile time, and a system test decodes every sim set against the SRDB.
  The MSP430 no longer sends SID 1, whose temperature and voltages it does
  not measure.
- Host sim: TM produced in a sensor tick (housekeeping, S12 events) was
  only written after the next frame. It now leaves before that tick's
  end-of-tick marker.

## [v0.17] — Qualification baseline

### Added

- `docs/SRS.md`: software requirements (OBSW-SRS-NNN) with area, status and
  verification method.
- Test traceability: C tests carry `@verifies` tags, Python tests
  `@pytest.mark.requirement`; `tools/trace.py` fails CI on untested
  requirements or unknown IDs, checks `libobsw.a` for allocator calls and
  writable globals, and publishes the matrix as a CI artifact.
- `test/system`: pytest suite driving `obsw_sim` over wire protocol v3
  (framing, SAFE gate, TM(1,2) codes, S6, S9, HK encoding, TC(3,1), S8 modes).
- SRDB loader tests.
- `OBSW_ENABLE_COVERAGE` CMake option and a CI coverage gate: core library
  line coverage below 90% fails the build; HTML report kept as an artifact.
- MSP430FR5969 build in CI on every pull request, with a FRAM/SRAM usage
  report; the TI toolchain is pinned by SHA-256 and cached.
- Static analysis in CI: cppcheck with the MISRA C:2012 addon on the core
  library. Required rules are at zero; Advisory deviations are documented in
  `docs/misra-deviations.md` (#74).

### Fixed

- TC(8,1) with an argument length larger than the TC carries was passed to
  the function callback; it is now rejected with TM(1,2) `0x0005`.
- Malformed user data in S3 enable/disable, S8 and S20 now reports `0x0005`
  (was `0x0003`), consistent with S6, S9 and TC(3,1).
- `obsw_adcs_set_target()` stored a zero-norm target, which silently
  stopped attitude control in NOMINAL. It now returns `bool` and keeps the
  previous target.

### Changed

- Core library brought to MISRA C:2012 Required-rule compliance: braced
  bodies, explicit `(void)` on ignored results, explicit Boolean tests and
  typed operands. No behaviour change.

## [v0.16] — Tell the truth

Consolidates the untagged v0.12–v0.15 work and makes the documentation match
what each build does.

### Breaking changes

- **TM(3,25) parameters are big-endian** (#82). They used to go out in host
  byte order (little-endian on every target). Ground decoders that used
  `'<f'` / `'<I'` for HK must switch to `'>'`. opensvf already decodes
  big-endian, so it now reads `obc_obt` and `obc_reset_count` correctly.
- **SAFE/STANDBY whitelist enforced on every target** (#86). The host sim and
  ZynqMP used to accept every TC in SAFE. Only TC(17,1), (8,1), (9,128) and
  (20,3) are now accepted until TC(8,1) function 1 commands NOMINAL.
- **Rejected TCs produce TM(1,2)** (#80). Codes: `0x0002` unknown service,
  `0x0003` unknown subservice, `0x0007` not allowed in the current mode.
  The FreeRTOS build used to report mode-gated TCs as `0x0001`.
- `obsw_s6_ctx_t` needs a region table; without one every S6 TC is rejected.

### Added

- PUS S9: TC(9,128) set on-board time, CUC-4,2 OBT with drift-free
  sub-millisecond accumulation (#73).
- S6 memory regions: ground addresses map through a table with READ/WRITE
  access; out-of-range requests get TM(1,2) `0x0006`. S6 is routed on the
  host sim (4 KB patch RAM at `0x0`, matching opensvf) and STM32H7 FreeRTOS
  (patch RAM plus read-only flash for image CRC checks).
- TC(3,1): define a housekeeping set at run time from S20 parameter IDs;
  routed on the host sim and STM32H7 FreeRTOS.
- Dispatcher acceptance gate (`obsw_tc_dispatcher_set_gate`) and
  `obsw_s1_dispatch_responder`; one SAFE whitelist generated from
  `safe_allowed` in `srdb/data/telecommands.yaml`.
- AOCS: B-dot harness and orbit SRDB parameters (v0.12), NOMINAL PD
  harness and S20 config contract (v0.13), nadir-pointing LVLH target
  (v0.14), AOCS state HK and ZynqMP nadir parity (v0.15).
- B-dot washout high-pass filter on dB/dt, `bdot_hpf_tau` (0x20A3,
  default 15 s), retunable by TC(20,1) (#70; opensvf-kde validation open).
- ZynqMP: S8 mode transitions and NOMINAL AOCS Renode test.
- STM32H750: I2C1 HAL and QMC5883L magnetometer driver; FDIR-owned S3 sets
  and ST7735R power-supervisor keepalive.
- SRDB additive composition and materialization for external build input;
  OrbitFabric event materialization in the host sim.
- `OBSW_WERROR` CMake option; CI builds with `-Werror`, runs SRDB pytest,
  keeps target binaries for 90 days and runs monthly.
- `scripts/setup-workspace.sh`: idempotent toolchain setup for the dev
  container (gh, clang tools, cross toolchains, TI msp430-gcc, Renode).

### Fixed

- TC(6,2) could read up to 64 KB past the end of the TC buffer.
- PUS task stack on FreeRTOS raised to 4 KB so a full TC(6,9) dump fits.
- ADCS error-quaternion sign; Renode PUS gate offsets and EOT parsing;
  weak `USART3_IRQHandler` so the non-FreeRTOS STM32H7 build links.
- SRDB composition test fixtures no longer collide with base HK set IDs.

### Documentation

- README rewritten: targets with the evidence behind each, routed services
  per target, test and coverage figures, release-train roadmap. "Flight-grade"
  replaced by "flight-oriented".
- `docs/mission-config.md`: S6 memory map and SAFE whitelist sections.
