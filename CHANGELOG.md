# Changelog

All notable changes to openobsw. Versions follow the release trains in the
README roadmap. Releases before v0.16 are described on the
[GitHub releases page](https://github.com/lipofefeyt/openobsw/releases).

## [Unreleased] — v0.18 Service parity with OpenSVF

### Added

- TC(3,3) delete housekeeping sets created by TC(3,1), all or nothing;
  freed slots and parameter storage are reused (#91).
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
