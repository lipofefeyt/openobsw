# openobsw Software Requirements Specification

> **Ref:** OBSW-SRS-001  **Issue:** 1.0  **Baseline:** v0.17 (draft)
> **Author:** lipofefeyt

---

## 1. Scope

This document specifies the software requirements for the openobsw core
library and its reference builds (host sim, STM32H7 FreeRTOS, STM32H7
superloop, ZynqMP, MSP430). It is the baseline that tests trace to.

Requirements are derived from public standards only: CCSDS 132.0-B, 133.0-B
and 232.0-B, ECSS-E-ST-70-41C (PUS-C), and ECSS-E-ST-40C for structure.
Mission-specific values (SCID, APIDs, memory map, whitelist) are listed in
[mission-config.md](mission-config.md) and are outside this baseline.

## 2. Conventions

Each requirement is written as:

```
**OBSW-SRS-NNN** `[AREA]` `STATUS` `METHOD`
The software shall ...
```

IDs are grouped in blocks of ten or twenty per area so that new requirements
can sit next to related ones. IDs are never reused.

### Areas

| Tag | Area | Block |
|---|---|---|
| [GEN] | Design constraints | 001–019 |
| [TCF] | TC transfer frame | 020–029 |
| [SPP] | Space packet | 030–039 |
| [TMF] | TM transfer frame | 040–049 |
| [TMS] | TM store | 050–059 |
| [DSP] | TC dispatcher and acceptance | 060–079 |
| [S1] | TC verification | 080–089 |
| [S3] | Housekeeping | 090–109 |
| [S5] | Event reporting | 110–119 |
| [S6] | Memory management | 120–139 |
| [S8] | Function management | 140–149 |
| [S9] | Time management | 150–159 |
| [S17] | Test | 160–169 |
| [S20] | Parameter management | 170–179 |
| [MOD] | Mode management and FDIR | 180–199 |
| [AOC] | Attitude and orbit control | 200–219 |
| [SDB] | Spacecraft Resource Database | 220–239 |
| [SIM] | Host simulation interface | 240–259 |
| [QA] | Product assurance | 260–279 |

### Status

| Status | Meaning |
|---|---|
| DRAFT | Under discussion, not yet baselined |
| BASELINED | Agreed for the current train, not yet implemented |
| IMPLEMENTED | Implemented on `main` and verified by the stated method |
| DEFERRED | Out of scope for the current train, kept for later |
| SUPERSEDED | Replaced by a later requirement |

### Verification methods (ECSS-E-ST-40C)

| Code | Method | Evidence |
|---|---|---|
| T | Test | A test tagged `@verifies` the ID (C: comment above the Unity test; Python: `@pytest.mark.requirement`) |
| A | Analysis | Reasoned argument or measurement recorded with the requirement |
| I | Inspection | Review of code, build or CI configuration (including CI job results), named with the requirement |
| R | Review of design | Design document named with the requirement |

`tools/trace.py` checks that every IMPLEMENTED requirement with method T has
at least one test, that every tag names a known requirement, and writes the
traceability matrix.

---

## 3. Design constraints [GEN]

**OBSW-SRS-001** `[GEN]` `IMPLEMENTED` `T`
The core library shall not call dynamic memory allocation functions (`malloc`, `calloc`, `realloc`, `free`).
*Evidence: `tools/trace.py --lib` symbol check `trace::no_allocator` on the built `libobsw.a`.*

**OBSW-SRS-002** `[GEN]` `IMPLEMENTED` `T`
Core library functions shall take all state through caller-supplied context pointers and shall not keep mutable state at file or global scope.
*Evidence: `tools/trace.py --lib` symbol check `trace::no_writable_globals`: `libobsw.a` defines no data or BSS symbols.*

**OBSW-SRS-003** `[GEN]` `IMPLEMENTED` `I`
The core library shall perform platform I/O only through the `obsw_io_ops_t` read/write interface.
*Evidence: inspection; the core includes no HAL or platform headers.*

**OBSW-SRS-004** `[GEN]` `IMPLEMENTED` `I`
The software shall be written in C11 and build without warnings under `-Wall -Wextra -Wpedantic -Werror` with GCC and Clang.
*Evidence: CI `gcc`/`clang` jobs build with `OBSW_WERROR=ON`.*

**OBSW-SRS-005** `[GEN]` `IMPLEMENTED` `I`
Buffer lengths in the core API shall be `uint16_t` so that the same code builds for 16-bit targets.
*Evidence: MSP430 build; API inspection.*

**OBSW-SRS-006** `[GEN]` `IMPLEMENTED` `I`
The core library shall run without memory errors or undefined behaviour under AddressSanitizer and UndefinedBehaviorSanitizer for the full test suite.
*Evidence: CI `asan` job.*

---

## 4. TC transfer frame [TCF]

**OBSW-SRS-020** `[TCF]` `IMPLEMENTED` `T`
The software shall decode the 5-byte TC transfer frame primary header (CCSDS 232.0-B), including the 10-bit spacecraft ID and the frame length.

**OBSW-SRS-021** `[TCF]` `IMPLEMENTED` `T`
The software shall reject a TC transfer frame whose frame error control field does not match the CRC-16/CCITT (polynomial 0x1021, initial value 0xFFFF) computed over the rest of the frame.

**OBSW-SRS-022** `[TCF]` `IMPLEMENTED` `T`
The software shall reject a TC transfer frame that is shorter than its minimum length or whose declared length disagrees with the received length or exceeds the buffer.

**OBSW-SRS-023** `[TCF]` `IMPLEMENTED` `T`
The software shall use a single CRC-16/CCITT implementation for TC frame checking, TM frame FECF generation and S6 memory checks.

---

## 5. Space packet [SPP]

**OBSW-SRS-030** `[SPP]` `IMPLEMENTED` `T`
The software shall encode and decode the 6-byte space packet primary header (CCSDS 133.0-B), including version, type, secondary header flag, 11-bit APID, sequence flags, 14-bit sequence count and data length.

**OBSW-SRS-031** `[SPP]` `IMPLEMENTED` `T`
The software shall reject a space packet whose buffer is shorter than its primary header or than the length the header declares.

---

## 6. TM transfer frame [TMF]

**OBSW-SRS-040** `[TMF]` `IMPLEMENTED` `T`
The software shall build TM transfer frames (CCSDS 132.0-B) with a fixed data field length set by the caller, carrying the spacecraft ID, virtual channel ID and master and virtual channel frame counts.

**OBSW-SRS-041** `[TMF]` `IMPLEMENTED` `T`
The software shall fill any unused part of a TM frame data field with an idle space packet.

**OBSW-SRS-042** `[TMF]` `IMPLEMENTED` `T`
When FECF is enabled, the software shall append a CRC-16/CCITT frame error control field to each TM frame; when disabled, it shall not.

**OBSW-SRS-043** `[TMF]` `IMPLEMENTED` `T`
The TM frame builder shall reject null arguments and output buffers too small for the configured frame.

---

## 7. TM store [TMS]

**OBSW-SRS-050** `[TMS]` `IMPLEMENTED` `T`
The software shall buffer outgoing TM packets in a fixed-capacity FIFO, statically sized by `OBSW_TM_STORE_SLOTS` and `OBSW_TM_MAX_PACKET_LEN`.

**OBSW-SRS-051** `[TMS]` `IMPLEMENTED` `T`
The TM store shall reject a packet larger than `OBSW_TM_MAX_PACKET_LEN`, and shall reject a new packet when full without overwriting stored packets.

---

## 8. TC dispatcher and acceptance [DSP]

**OBSW-SRS-060** `[DSP]` `IMPLEMENTED` `T`
The software shall route each parsed TC to the first entry of a static routing table matching its APID (or a wildcard APID), service type and subtype.

**OBSW-SRS-061** `[DSP]` `IMPLEMENTED` `T`
The software shall reject a TC that matches no route, without calling any handler, and report it with TM(1,2): code `0x0002` when no route exists for the service, `0x0003` when the service is routed but the subtype is not.

**OBSW-SRS-062** `[DSP]` `IMPLEMENTED` `T`
The software shall support an acceptance gate that is consulted before routing; a TC refused by the gate shall not reach its handler and shall be reported with TM(1,2).

**OBSW-SRS-063** `[DSP]` `IMPLEMENTED` `T`
Every reference build with a mode machine shall install the mode gate (OBSW-SRS-182) on its dispatcher.
*Evidence: `test_tc_pipeline`; `test/system` for the host sim.*

**OBSW-SRS-064** `[DSP]` `IMPLEMENTED` `T`
The software shall drop, without a response, a TC packet that cannot be parsed or lacks a PUS secondary header.

---

## 9. TC verification [S1]

**OBSW-SRS-080** `[S1]` `IMPLEMENTED` `T`
The software shall report TC acceptance success as TM(1,1), acceptance failure as TM(1,2), completion success as TM(1,7) and completion failure as TM(1,8), each carrying the TC packet ID and sequence control.

**OBSW-SRS-081** `[S1]` `IMPLEMENTED` `T`
TM(1,2) and TM(1,8) shall carry a 16-bit failure code: `0x0002` unknown service, `0x0003` unknown subtype, `0x0004` execution error, `0x0005` malformed or invalid application data, `0x0006` memory address not accessible, `0x0007` TC not allowed in the current mode.

**OBSW-SRS-082** `[S1]` `IMPLEMENTED` `T`
Every TC handler shall issue an acceptance report (TM(1,1) or TM(1,2)) before executing, and a completion report (TM(1,7) or TM(1,8)) after a successful acceptance.

**OBSW-SRS-083** `[S1]` `IMPLEMENTED` `T`
Every TC handler shall reject truncated or inconsistent user data with TM(1,2) code `0x0005` before reading beyond the TC's user data field.

---

## 10. Housekeeping [S3]

**OBSW-SRS-090** `[S3]` `IMPLEMENTED` `T`
The software shall emit TM(3,25) for each enabled housekeeping set when its collection interval, counted in control ticks, elapses, and reload the interval.

**OBSW-SRS-091** `[S3]` `IMPLEMENTED` `T`
The software shall enable a set and set its interval on TC(3,5), and disable it on TC(3,6); an unknown set ID shall be rejected with TM(1,2).

**OBSW-SRS-092** `[S3]` `IMPLEMENTED` `T`
TM(3,25) shall carry the set ID followed by each parameter in definition order, big-endian, at its declared size (1, 2 or 4 bytes; 4-byte floats as IEEE 754).

**OBSW-SRS-093** `[S3]` `IMPLEMENTED` `T`
The software shall create a housekeeping set on TC(3,1) from a set ID, interval and list of S20 parameter IDs; the set shall start disabled and report the parameters' live values.

**OBSW-SRS-094** `[S3]` `IMPLEMENTED` `T`
The software shall reject TC(3,1) without changing state when the set ID exists, a parameter ID is unknown, the parameter count is zero or disagrees with the length, or the report would not fit one TM packet (code `0x0005`), and when no set slot or parameter storage is left (code `0x0004`).

**OBSW-SRS-095** `[S3]` `IMPLEMENTED` `T`
The software shall emit TM(3,25) for a set on demand, independent of its schedule.

**OBSW-SRS-096** `[S3]` `BASELINED` `T`
The software shall delete housekeeping sets created by TC(3,1) on TC(3,3), and refuse to delete boot-time sets or enabled sets. *(#91)*

---

## 11. Event reporting [S5]

**OBSW-SRS-110** `[S5]` `IMPLEMENTED` `T`
The software shall report events as TM(5,1) to TM(5,4) for informational, low, medium and high severity, carrying the event ID and any auxiliary data.

**OBSW-SRS-111** `[S5]` `IMPLEMENTED` `T`
A high-severity event whose ID is in the configured safe-trigger list shall command the mode machine to SAFE; other events shall not change the mode.

---

## 12. Memory management [S6]

**OBSW-SRS-120** `[S6]` `IMPLEMENTED` `T`
The software shall write the data of TC(6,2) to memory at the given ground address.

**OBSW-SRS-121** `[S6]` `IMPLEMENTED` `T`
The software shall compute a CRC-16/CCITT over a memory range on TC(6,5), and report TM(6,10) if it matches the expected value or TM(6,11) with the computed value if not.

**OBSW-SRS-122** `[S6]` `IMPLEMENTED` `T`
The software shall report the contents of a memory range as TM(6,6) on TC(6,9), and reject a dump larger than one TM packet.

**OBSW-SRS-123** `[S6]` `IMPLEMENTED` `T`
The software shall serve an S6 request only if its whole address range lies inside one configured region granting the needed access (write for load, read for check and dump), and otherwise reject it with TM(1,2) code `0x0006` before touching memory. With no regions configured, every S6 request shall be rejected.

**OBSW-SRS-124** `[S6]` `IMPLEMENTED` `T`
Address range checks shall be free of integer overflow, including ranges near the top of the 32-bit address space.

---

## 13. Function management [S8]

**OBSW-SRS-140** `[S8]` `IMPLEMENTED` `T`
The software shall execute the function registered under the function ID of TC(8,1), passing it the TC's argument bytes, and report TM(1,8) if the function fails.

**OBSW-SRS-141** `[S8]` `IMPLEMENTED` `T`
The software shall reject TC(8,1) for an unknown function ID (code `0x0004`) or an argument length larger than the TC carries (code `0x0005`), without calling any function.

**OBSW-SRS-142** `[S8]` `IMPLEMENTED` `T`
The reference builds shall register function 1 to command NOMINAL, function 2 to command SAFE and function 3 to command STANDBY.
*Evidence: `test/system` on the host sim; Renode ZynqMP NOMINAL test.*

---

## 14. Time management [S9]

**OBSW-SRS-150** `[S9]` `IMPLEMENTED` `T`
The software shall keep on-board time as CUC-4,2: 32-bit seconds and a 16-bit fraction of 1/65536 s.

**OBSW-SRS-151** `[S9]` `IMPLEMENTED` `T`
On-board time shall advance by the elapsed time of each control tick with no accumulated rounding error over repeated ticks.

**OBSW-SRS-152** `[S9]` `IMPLEMENTED` `T`
The software shall set on-board time to the value carried by TC(9,128) and reject a TC with fewer than 6 bytes of user data.

---

## 15. Test [S17]

**OBSW-SRS-160** `[S17]` `IMPLEMENTED` `T`
The software shall answer TC(17,1) with TM(1,1), TM(17,2) and TM(1,7), in that order.

---

## 16. Parameter management [S20]

**OBSW-SRS-170** `[S20]` `IMPLEMENTED` `T`
The software shall set the 32-bit raw value of a parameter on TC(20,1) and report it as TM(20,2) on TC(20,3).

**OBSW-SRS-171** `[S20]` `IMPLEMENTED` `T`
The software shall reject TC(20,1) and TC(20,3) for an unknown parameter ID (code `0x0004`) or truncated user data (code `0x0005`).

---

## 17. Mode management and FDIR [MOD]

**OBSW-SRS-180** `[MOD]` `IMPLEMENTED` `T`
The software shall provide the modes STANDBY, SAFE and NOMINAL, start in STANDBY, and allow SAFE ↔ NOMINAL and any mode → STANDBY transitions.

**OBSW-SRS-181** `[MOD]` `IMPLEMENTED` `T`
The software shall call the configured entry hook on each transition into SAFE, count SAFE entries, and call the exit hook only when leaving SAFE.

**OBSW-SRS-182** `[MOD]` `IMPLEMENTED` `T`
In SAFE and STANDBY, the software shall accept only the TCs on the configured whitelist and reject all others with TM(1,2) code `0x0007`; in NOMINAL it shall accept all routed TCs.

**OBSW-SRS-183** `[MOD]` `IMPLEMENTED` `T`
The SAFE/STANDBY whitelist shall be generated from the SRDB (`safe_allowed`) and shall be the same on every reference build.
*Evidence: SRDB codegen test; inspection that every build uses `SRDB_SAFE_TC_WHITELIST`.*

**OBSW-SRS-184** `[MOD]` `IMPLEMENTED` `T`
The software shall provide a software watchdog that calls its expiry handler once when not kicked for the configured number of ticks; a kick shall reload the count.

**OBSW-SRS-185** `[MOD]` `IMPLEMENTED` `T`
Watchdog expiry shall raise a high-severity event that commands SAFE.

**OBSW-SRS-186** `[MOD]` `IMPLEMENTED` `T`
Mode values reported in telemetry shall use the SRDB `obc_mode` enumeration (STANDBY=0, SAFE=1, NOMINAL=2), independent of the internal encoding.

---

## 18. Attitude and orbit control [AOC]

**OBSW-SRS-200** `[AOC]` `IMPLEMENTED` `T`
In SAFE, the software shall command magnetorquer dipoles m = −k·dB/dt from consecutive magnetometer samples, outputting zero on the first sample.

**OBSW-SRS-201** `[AOC]` `IMPLEMENTED` `T`
The B-dot controller shall limit each dipole component to the configured maximum.

**OBSW-SRS-202** `[AOC]` `IMPLEMENTED` `T`
The B-dot controller shall high-pass filter dB/dt with a first-order washout of time constant τ (`bdot_hpf_tau`), attenuating slow field drift while passing attitude-rate content; τ = 0 shall disable the filter.

**OBSW-SRS-203** `[AOC]` `IMPLEMENTED` `T`
In NOMINAL, the software shall command reaction-wheel torque τ = −Kp·q_err,vec − Kd·ω from the star-tracker quaternion, the target quaternion and gyro rate, limiting each component to the configured maximum.

**OBSW-SRS-204** `[AOC]` `IMPLEMENTED` `T`
Controller gains (`bdot_gain`, `bdot_hpf_tau`, `adcs_kp`, `adcs_kd`) shall be changeable at run time through S20.

**OBSW-SRS-205** `[AOC]` `IMPLEMENTED` `T`
Quaternion operations shall reject a zero-norm quaternion and null inputs instead of producing undefined values.

---

## 19. Spacecraft Resource Database [SDB]

**OBSW-SRS-220** `[SDB]` `IMPLEMENTED` `T`
The SRDB shall define parameters, telecommands, events and housekeeping sets in YAML and validate IDs, ranges, types and cross-references on load.

**OBSW-SRS-221** `[SDB]` `IMPLEMENTED` `T`
The SRDB shall generate a C header with parameter, event, HK set and telecommand identifiers, the safe-trigger event list, the SAFE TC whitelist and parameter enumeration values.

**OBSW-SRS-222** `[SDB]` `IMPLEMENTED` `T`
The SRDB shall generate an XTCE 1.2 mission database whose HK containers match the TM(3,25) layout produced on board.

**OBSW-SRS-223** `[SDB]` `IMPLEMENTED` `T`
The SRDB shall compose a base database with contributions, rejecting ID or name collisions, without modifying the base.

**OBSW-SRS-224** `[SDB]` `IMPLEMENTED` `T`
The SRDB shall export to and import from CSV without loss.

**OBSW-SRS-225** `[SDB]` `BASELINED` `T`
The SRDB shall describe telecommands with a repeated argument whose count is given by an earlier argument, and export them to XTCE. *(#92)*

---

## 20. Host simulation interface [SIM]

**OBSW-SRS-240** `[SIM]` `IMPLEMENTED` `T`
The host sim shall exchange framed messages over stdin/stdout: TC (0x01) and sensor (0x02) in; actuator (0x03), TM (0x04) and end-of-tick (0xFF) out, each with a big-endian 16-bit length.

**OBSW-SRS-241** `[SIM]` `IMPLEMENTED` `T`
The host sim shall answer every TC frame with all TM it produces before the next end-of-tick marker.

**OBSW-SRS-242** `[SIM]` `IMPLEMENTED` `T`
The host sim shall boot in SAFE.

**OBSW-SRS-243** `[SIM]` `IMPLEMENTED` `T`
The host sim shall route S3, S6, S8, S9, S17 and S20 and expose 4 KB of readable and writable S6 memory at ground address 0.

---

## 21. Product assurance [QA]

**OBSW-SRS-260** `[QA]` `IMPLEMENTED` `I`
Each release shall be tagged and described in `CHANGELOG.md`.

**OBSW-SRS-261** `[QA]` `IMPLEMENTED` `I`
CI shall fail when an IMPLEMENTED requirement with method T has no tagged test, or a test names an unknown requirement.
*Evidence: `tools/trace.py` CI step.*

**OBSW-SRS-262** `[QA]` `IMPLEMENTED` `I`
CI shall measure line coverage of the core library and fail below 90%.
*Evidence: CI `coverage` job (`gcovr --fail-under-line 90` over `src/{ccsds,tc,tm,pus,fdir,aocs,time}`, after unit, integration and system tests).*

**OBSW-SRS-263** `[QA]` `IMPLEMENTED` `I`
CI shall run static analysis on the core library with MISRA C:2012 checks, with no findings against Mandatory or Required rules, and fail on any finding not covered by a documented deviation. *(#74)*
*Evidence: CI `static-analysis` job (cppcheck + `misra` addon, `--error-exitcode=1`); deviations in [misra-deviations.md](misra-deviations.md).*

**OBSW-SRS-264** `[QA]` `IMPLEMENTED` `I`
CI shall build the MSP430 target on every pull request and report its memory use.
*Evidence: CI `build-msp430` job (TI msp430-elf-gcc 9.3.1, pinned by SHA-256; linker enforces the 2 KB SRAM and 48 KB FRAM limits).*
