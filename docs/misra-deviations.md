# MISRA C:2012 compliance and deviations

> **Scope:** the core library (`src/{ccsds,tc,tm,pus,fdir,aocs,time}` and the
> headers it includes). HAL, platform entry points, FreeRTOS tasks and the host
> sim are outside it.
> **Tool:** cppcheck 2.13 with its `misra` addon, run by the `static-analysis`
> CI job (OBSW-SRS-263).

This document names rules by number only. The MISRA rule texts are licensed
and are not reproduced here; the short topic after each number is a
paraphrase for orientation.

## Policy

- Every **Mandatory and Required** rule the tool checks is enforced at zero
  findings. There are no Required deviations.
- **Advisory** rules are either deviated permanently by design (section 1)
  or not enforced yet (section 2). Both are listed in
  [`tools/cppcheck-suppressions.txt`](../tools/cppcheck-suppressions.txt),
  which is the only place the CI job suppresses anything.
- cppcheck's own `warning`, `style`, `performance` and `portability` checks
  are also enforced at zero.
- Any new finding fails CI. Adding a suppression needs an entry here.

## Notes on enforced rules

| Rule | How the code complies |
|---|---|
| 11.1 (function-pointer conversions) | Handlers that don't use the `respond` callback discard it with `(void)&respond;`, which converts the address of the parameter object rather than the function pointer itself |
| 14.4 (Boolean controlling expressions) | Flag and pointer tests compare explicitly: `(x & MASK) != 0U`, `p != NULL` |
| 15.6 (compound statement bodies) | Every `if`, `else`, `for` and `while` body is braced |
| 17.7 (unused return values) | Calls whose result is deliberately ignored are cast to `(void)`. For S1 and TM reports this marks the intended best-effort behaviour: a report that does not fit the TM store is dropped, and the TC outcome does not change |

## 1. Permanent deviations (Advisory, by design)

| Rule | Topic (paraphrase) | Findings | Rationale |
|---|---|---|---|
| 15.5 | single point of exit | 126 | Argument and TC validation use early-return guard clauses. Single-exit rewrites add nesting without making the checks clearer |
| 11.5 | `void *` to object pointer | 13 | Handlers and callbacks receive their context as `void *`, the standard way to pass typed state through a generic callback without globals (OBSW-SRS-002). Each handler converts it back to its own context type |
| 18.4 | `+`/`-` pointer arithmetic | 21 | Frame and packet code addresses fields as offsets into caller buffers. Every offset is bounds-checked against the buffer length first (OBSW-SRS-031, -083, -123) |
| 19.2 | unions | 4 | `obsw_s20_value_t` holds an S20 raw 32-bit value as `u32`, `i32` or `f32`. Reinterpreting the same bits is the purpose of the type |
| 11.4 | object pointer to integer | 1 | S6 region bases are `const` so read-only regions can cover flash. A write to a region granted WRITE goes through `uintptr_t` to drop `const` (`src/pus/s6.c`). Access is checked before the conversion (OBSW-SRS-123) |

## 2. Not enforced yet (Advisory)

To be brought to zero and removed from the suppressions in later trains.

| Rule | Topic (paraphrase) | Findings |
|---|---|---|
| 12.1 | explicit operator precedence | 47 |
| 13.3 | `++`/`--` mixed with other side effects | 22 |
| 2.5 | unused macros | 16 |
| 8.7 | external linkage for single-unit use | 4 |
| 13.4 | assignment result used | 3 |
| 5.9 | unique internal identifiers | 2 |
| 2.3 | unused type declarations | 2 |
| 2.4 | unused tag declarations | 1 |
| 12.3 | comma operator | 1 |

Counts are from the v0.17 baseline.
