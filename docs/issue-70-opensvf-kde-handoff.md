# Issue #70 — B-dot orbital aliasing: opensvf-kde validation handoff

**Status:** fix implemented and bench-validated in openobsw; needs a run against
the opensvf-kde `OrbitalEnvironment` model to formally close and lift the
`bdot_gain` freeze.

**openobsw branch:** `fix/70-bdot-hpf-orbital-aliasing` — commits `dede8c0`
(HPF) and `d594d31` (τ tuning + S20 registration + bench harness).

---

## 1. The bug

`obsw_bdot_step()` (`src/aocs/bdot.c`) estimates the field derivative by raw
finite difference:

```
dB/dt ≈ (B_k − B_{k-1}) / dt
```

Under opensvf-kde's `OrbitalEnvironment`, the body-frame magnetometer reading
changes as the spacecraft travels along its orbit **even at zero attitude
rate** — the geomagnetic field has a spatial gradient. The finite-difference
estimator cannot tell "the field changed because I rotated" from "the field
changed because I moved 50 km along-track". The orbital component aliases into
a non-zero `dB/dt`, so B-dot commands a spurious dipole forever and never
quiesces.

Measured baseline (opensvf-kde, null attitude):
`|dB/dt| ≈ 4.11e-08 T/s → |m_cmd| ≈ 4.11e-04 Am²` with zero tumble.
The KDE/FMU path did **not** show this (B held constant for a null pass) —
aliasing is specific to the `OrbitalEnvironment` spatial model.

---

## 2. The fix

First-order washout (high-pass) on the `dB/dt` estimate, applied before the
B-dot gain:

```
y[k] = α · (y[k-1] + x[k] − x[k-1])          α = τ / (τ + dt)
```

- `x[k]` = raw finite-difference `dB/dt`, `y[k]` = filtered `dB/dt`
- DC (the orbital gradient, ~1.7e-4 Hz at 550 km) → 0 in steady state
- attitude-tumble content (≫ corner frequency) passes with ~unity gain
- `τ = 0` disables the filter (`y = x`), preserving the original behaviour

Implemented in `obsw_bdot_step()`; state carried in `obsw_bdot_ctx_t`
(`dbdt_raw_prev`, `dbdt_filt`). Output struct `obsw_bdot_output_t` now carries
both `dbdt[3]` (raw) and `dbdt_filt[3]` (filtered) for telemetry/diagnostics.

### Parameter

| Field | Value |
|---|---|
| SRDB name | `bdot_hpf_tau` |
| SRDB id | `0x20A3` |
| Type | `float32`, seconds |
| Default | **15 s** |
| Disable | `0` |
| Limits | `soft_low: 0.0` |
| Path | `srdb/data/parameters.yaml` → generated `SRDB_PARAM_BDOT_HPF_TAU` |

Registered in the S20 tables of both `sim/main.c` and `src/task/pus.c`, so
ground can retune it live with `TC(20,1)` on `0x20A3` and read it with
`TC(20,3)`. Re-synced from the S20 store into `bdot_ctx.config.hpf_tau` on
every control tick (host: `sim/main.c`; flight: `src/task/aocs.c`).

### Why τ = 15 s

The washout coefficient `α = τ/(τ+dt)` → 1 as τ grows, so an over-large τ
degrades the filter toward passthrough. Bench sweep (see §4) — steady-state
spurious `|m_cmd|` rejection vs `τ=0`:

| τ (s) | rejection | detumble convergence (ω₀ = 0.3 rad/s) |
|------:|----------:|--------------------------------------:|
| 15 | **8.9×** | 0.35 orbits |
| 30 | 5.9× | 0.25 orbits |
| 60 | 5.0× | — |

τ = 15 s gives the best artifact rejection at negligible detumble cost. This
default may want revisiting for a different orbit altitude / `dt`; that is
exactly what the opensvf-kde run should check.

---

## 3. What to run on opensvf-kde

Reproduce the openobsw bench (`sim/bdot_hpf_validation.py`) but feed the
magnetometer from the real `OrbitalEnvironment` instead of the built-in
geocentric-dipole model. Two checks:

### Check A — orbital-aliasing rejection (null attitude)

- Hold attitude fixed (or start from rest), `ω₀ = 0`.
- Let `OrbitalEnvironment` drive `B_body(t)` over ≥ 2 orbits.
- Sweep `bdot_hpf_tau ∈ {0, 15, 30}` via `TC(20,1)`.
- **Pass:** at the chosen τ, steady-state `|m_cmd|` (mean over the last orbit)
  drops ≥ 5× vs τ=0 **and** the from-rest spurious `|ω|` peak stays
  < 5e-3 rad/s. Expect `|m_cmd|` to fall from ~4.11e-4 Am² toward ~5e-5 Am²
  or below.

### Check B — detumble still works

- Full rigid-body dynamics, `ω₀ = (0.3, 0.3, 0.3) rad/s`, chosen τ.
- **Pass:** `|ω| < 0.05 rad/s` within 3 orbital periods.

### Wire protocol (v3, framed pipe — stdin/stdout of `obsw_sim`)

```
→ OBSW  0x01 [u16 BE len] [TC space-packet bytes]       TC uplink
→ OBSW  0x02 [u16 BE len] [obsw_sensor_frame_t]         Sensor injection
← OBSW  0x03 [u16 BE len] [obsw_actuator_frame_t]       Actuator output
← OBSW  0x04 [u16 BE len] [TM packet bytes]             TM downlink
← OBSW  0xFF                                            End of tick
```

- `obsw_sensor_frame_t` — 47 B packed LE, `struct '<fffBffffBfffBf'`:
  MAG xyz + valid, ST quat wxyz + valid, GYRO xyz + valid, sim_time.
  For B-dot only MAG xyz + `mag_valid=1` matter; set ST/GYRO valid = 0.
- `obsw_actuator_frame_t` — 29 B packed LE, `struct '<ffffffBf'`:
  MTQ dipole xyz, RW torque xyz, controller byte, sim_time.
  B-dot output is `mtq_dipole_{x,y,z}`; `controller` byte = 0 in SAFE.
- **Set `bdot_hpf_tau`:** `TC(20,1)`, app data = `>H` param_id (`0x20A3`) then
  `>f` value (IEEE-754 BE). Build with `build_tc(APID, 20, 1, payload)` and
  `send_tc`; drain the response with `drain_tc_response`. Helpers in
  `sim/wire_proto.py`.
- The sim boots in **SAFE**, so B-dot is active with no mode TC needed.
- `obsw_sim` also logs per SAFE tick to stderr:
  `[OBSW] bdot raw=[…] |raw|=… filt=[…] |filt|=… m=[…] Am2` — capture this
  for the before/after comparison.

### Reference harness

`sim/bdot_hpf_validation.py` in this branch. Structure to reuse:
`run_aliasing()` / `run_detumble()` step the loop, `set_hpf_tau()` sends the
TC(20,1). Replace the `orbit_position()` / `igrf_dipole_eci()` calls with the
`OrbitalEnvironment` B-field lookup for the current sim time and
spacecraft position. `sim/bdot_harness.py` is the existing closed-loop
convergence harness and reads orbit/inertia config from S20 via `TC(20,3)`.

---

## 4. openobsw bench result (geocentric-dipole model, NOT KDE)

550 km SSO, inc 97.4°, `dt = 0.1 s`:

```
== Check A: spurious spin-up from rest ==
  tau=  0.0 s : peak|ω|=2.911e-03 rad/s   mean spurious|m_cmd|=1.871e-04 Am²
  tau= 15.0 s : peak|ω|=2.848e-04 rad/s   mean spurious|m_cmd|=2.098e-05 Am²  (8.9x)
  tau= 30.0 s : peak|ω|=5.743e-04 rad/s   mean spurious|m_cmd|=3.148e-05 Am²  (5.9x)
  tau= 60.0 s : peak|ω|=9.637e-04 rad/s   mean spurious|m_cmd|=3.770e-05 Am²  (5.0x)
== Check B: detumble (ω0=0.3,0.3,0.3, tau=15) ==
  converged at t=1994 s (0.35 orbital periods)  |ω|=0.0500 rad/s
PASS
```

The dipole bench's τ=0 spin-up already stays sub-threshold (2.9e-3 rad/s) —
the KDE model's steeper gradient is where the filter matters more, so the KDE
run is the one that counts.

---

## 5. After the opensvf-kde run passes

1. If the KDE run suggests a different τ, update the default in
   `srdb/data/parameters.yaml` (`0x20A3` description) and every code fallback
   (`sim/main.c` ×3, `src/task/aocs.c` ×2, `src/task/pus.c` ×1). `grep -rn
   BDOT_HPF_TAU` finds them all.
2. Lift the `bdot_gain` (`0x20A0`) retune freeze — remove the warning from
   `CLAUDE.md` "Open Issues" and close #70.
3. Full OpenSVF closed-loop integration test (YAMCS in the loop): confirm
   `bdot_hpf_tau` shows up as an XTCE parameter, is settable from the ground
   segment, and that a tumbling→detumbled campaign run stays nominal.

---

## 6. OrbitFabric contract sync (PoC point 4)

`bdot_hpf_tau` is currently defined only in `srdb/data/parameters.yaml`. Per
the #70 constraint it should be a contract-defined item so the generated
flight header and `parameters.yaml` agree. Add to
`orbitfabric_models/` in `lipofefeyt/OrbitFabric-OpenOBSW-PoC`:

```yaml
# parameter
name: bdot_hpf_tau
id: 0x20A3
type: float32
unit: s
default: 15.0
limits: { soft_low: 0.0 }
description: >
  B-dot HPF time constant. First-order high-pass on the dB/dt estimate that
  rejects orbital-rate field variation while passing attitude-rate content.
  0 disables. Bench default 15 s (550 km SSO); confirm against opensvf-kde.
subsystem: AOCS/Control
```

Then regenerate `generated_artifacts/flight_software/mission_contract.h` and
`generated_artifacts/ground_segment/*.xtce` and diff against openobsw's
`build/include/obsw/srdb_generated.h` / `build/xtce/mission.xtce`.
