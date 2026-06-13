#!/usr/bin/env python3
"""
sim/adcs_harness.py — Closed-loop PD ADCS (NOMINAL mode) nadir-pointing harness.

Tests that the proportional-derivative attitude controller drives the
spacecraft from an initial pointing error to within 2° of the nadir-pointing
target attitude within 120 s.

Physics
-------
Circular orbit rigid-body attitude dynamics:

  I·ω̇ = τ_rw − ω × (I·ω)      (Euler equations)
  q̇   = ½ q ⊗ [0, ω]          (quaternion kinematics)

ADCS target: nadir-pointing LVLH frame (body -z toward Earth,
body +x along-track, body +y orbit normal).  The target quaternion
q_nadir(t) rotates with the orbit; both the OBSW and the harness
compute it independently from the same S20 orbit parameters.

Spacecraft config is read from OBSW S20 via TC(20,3) at startup.

Pass criterion
--------------
  attitude_error_to_nadir < 2° within 120 s

Usage
-----
  source .venv/bin/activate
  python3 sim/adcs_harness.py [--sim PATH] [--dt DT] [--angle-deg DEG]
  adcs-harness          # activate.sh alias
"""

import argparse
import math
import os
import struct
import subprocess
import sys

import numpy as np

from wire_proto import (
    SRDB_SC_INERTIA_XX, SRDB_SC_INERTIA_YY, SRDB_SC_INERTIA_ZZ,
    SRDB_ADCS_KP, SRDB_ADCS_KD,
    SRDB_ORBIT_ALTITUDE_KM, SRDB_ORBIT_INCLINATION_DEG,
    build_tc, send_tc, drain_tc_response,
    send_sensor, recv_tick, query_s20_float,
    quat_step, omega_step,
    APID_DEFAULT,
)

# ── Physical constants ────────────────────────────────────────────────────
MU_EARTH = 3.986004418e14   # m³/s²
R_EARTH  = 6.371e6           # m

PASS_THRESHOLD_DEG = 2.0
PASS_THRESHOLD_RAD = math.radians(PASS_THRESHOLD_DEG)

NO_FIELD = np.zeros(3)       # mag_valid=0 in NOMINAL; ADCS uses ST+gyro only


# =========================================================================
# Quaternion math
# =========================================================================

def quat_multiply(a, b):
    """Hamilton product a ⊗ b, [w, x, y, z] convention."""
    wa, xa, ya, za = float(a[0]), float(a[1]), float(a[2]), float(a[3])
    wb, xb, yb, zb = float(b[0]), float(b[1]), float(b[2]), float(b[3])
    return np.array([
        wa*wb - xa*xb - ya*yb - za*zb,
        wa*xb + xa*wb + ya*zb - za*yb,
        wa*yb - xa*zb + ya*wb + za*xb,
        wa*zb + xa*yb - ya*xb + za*wb,
    ])


def quat_conjugate(q):
    return np.array([q[0], -q[1], -q[2], -q[3]])


def rot_matrix_to_quat(R):
    """Shepperd's method: 3×3 rotation matrix (body→ECI) → unit quaternion [w,x,y,z]."""
    tr = R[0, 0] + R[1, 1] + R[2, 2]
    if tr > 0:
        s = 0.5 / math.sqrt(tr + 1.0)
        w = 0.25 / s
        x = (R[2, 1] - R[1, 2]) * s
        y = (R[0, 2] - R[2, 0]) * s
        z = (R[1, 0] - R[0, 1]) * s
    elif R[0, 0] > R[1, 1] and R[0, 0] > R[2, 2]:
        s = 2.0 * math.sqrt(1.0 + R[0, 0] - R[1, 1] - R[2, 2])
        w = (R[2, 1] - R[1, 2]) / s
        x = 0.25 * s
        y = (R[0, 1] + R[1, 0]) / s
        z = (R[0, 2] + R[2, 0]) / s
    elif R[1, 1] > R[2, 2]:
        s = 2.0 * math.sqrt(1.0 + R[1, 1] - R[0, 0] - R[2, 2])
        w = (R[0, 2] - R[2, 0]) / s
        x = (R[0, 1] + R[1, 0]) / s
        y = 0.25 * s
        z = (R[2, 1] + R[1, 2]) / s
    else:
        s = 2.0 * math.sqrt(1.0 + R[2, 2] - R[0, 0] - R[1, 1])
        w = (R[1, 0] - R[0, 1]) / s
        x = (R[0, 2] + R[2, 0]) / s
        y = (R[2, 1] + R[1, 2]) / s
        z = 0.25 * s
    q = np.array([w, x, y, z])
    return q / np.linalg.norm(q)


def nadir_quat(t, r_orbit, inc_rad):
    """
    Body-to-ECI quaternion for nadir-pointing circular orbit at time t.

    Frame: body +z = zenith (anti-nadir), body -z = nadir (Earth-facing),
           body +x = along-track (velocity direction),
           body +y = orbit normal (r̂ × v̂).

    Must mirror compute_nadir_quat() in sim/main.c exactly.
    """
    omega_orb = math.sqrt(MU_EARTH / r_orbit**3)
    nu = omega_orb * t

    r = np.array([math.cos(nu),
                  math.sin(nu) * math.cos(inc_rad),
                  math.sin(nu) * math.sin(inc_rad)])
    v = np.array([-math.sin(nu),
                   math.cos(nu) * math.cos(inc_rad),
                   math.cos(nu) * math.sin(inc_rad)])

    h = np.cross(r, v)
    h /= np.linalg.norm(h)

    # R = [v̂ | h | r̂]: columns = body axes expressed in ECI
    R = np.column_stack([v, h, r])
    return rot_matrix_to_quat(R)


def quat_from_axis_angle(axis, angle_rad):
    """Unit quaternion [w, x, y, z] for rotation of angle_rad about axis."""
    axis = np.array(axis, dtype=float)
    axis /= np.linalg.norm(axis)
    s = math.sin(angle_rad / 2.0)
    return np.array([math.cos(angle_rad / 2.0),
                     axis[0]*s, axis[1]*s, axis[2]*s])


def attitude_error_rad(q_meas, q_target):
    """
    Angle [rad] between measured and target body-to-ECI quaternions.

    The OBSW uses q_err = q_target* ⊗ q_meas (body-frame error).
    This function uses q_meas ⊗ q_target* (ECI-frame error).
    Both produce conjugate quaternions with the same |w|, so the scalar
    angle is identical.  Do NOT use q_err.xyz from this function for
    directional purposes — use the OBSW convention for that.
    """
    q_err = quat_multiply(q_meas, quat_conjugate(q_target))
    return 2.0 * math.acos(min(1.0, abs(float(q_err[0]))))




# =========================================================================
# CLI
# =========================================================================

def parse_args():
    repo        = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    default_sim = os.path.join(repo, 'build', 'sim', 'obsw_sim')
    p = argparse.ArgumentParser(
        description='PD ADCS nadir-pointing closed-loop convergence harness (NOMINAL mode)',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument('--sim',       default=default_sim, metavar='PATH',
                   help='path to obsw_sim binary')
    p.add_argument('--dt',        type=float, default=0.01, metavar='S',
                   help='integration time step [s]')
    p.add_argument('--max-t',     type=float, default=120.0, metavar='S',
                   help='max simulation time before FAIL [s]')
    p.add_argument('--angle-deg', type=float, default=45.0,
                   help='initial attitude error from nadir, about body z [deg]')
    p.add_argument('--alt-km',   type=float, default=None,
                   help='orbit altitude [km] (default: read from OBSW S20)')
    p.add_argument('--inc-deg',  type=float, default=None,
                   help='orbit inclination [deg] (default: read from OBSW S20)')
    p.add_argument('--inertia',   nargs=3, type=float, default=None,
                   metavar=('IXX', 'IYY', 'IZZ'),
                   help='principal moments [kg·m²] (default: read from OBSW S20)')
    return p.parse_args()


# =========================================================================
# Main
# =========================================================================

def run(args):
    if not os.path.exists(args.sim):
        print(f'ERROR: sim binary not found: {args.sim}')
        print('  Build first: cmake --build build -j$(nproc)')
        return 2

    proc = subprocess.Popen(
        [args.sim],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )

    try:
        return _run(args, proc)
    except (EOFError, BrokenPipeError) as exc:
        print(f'\nERROR: sim process died unexpectedly: {exc}')
        return 1
    finally:
        try:
            proc.stdin.close()
        except OSError:
            pass
        proc.wait()


def _run(args, proc):
    dt = args.dt

    # ── Read config from OBSW S20 ─────────────────────────────────────────
    print('Reading spacecraft config from OBSW S20...')

    alt_km  = query_s20_float(proc, SRDB_ORBIT_ALTITUDE_KM,     550.0) if args.alt_km  is None else args.alt_km
    inc_deg = query_s20_float(proc, SRDB_ORBIT_INCLINATION_DEG,  97.4) if args.inc_deg is None else args.inc_deg

    if args.inertia:
        I_diag = args.inertia
        inertia_src = 'CLI'
    else:
        I_diag = [
            query_s20_float(proc, SRDB_SC_INERTIA_XX, 2.0e-3),
            query_s20_float(proc, SRDB_SC_INERTIA_YY, 2.5e-3),
            query_s20_float(proc, SRDB_SC_INERTIA_ZZ, 1.5e-3),
        ]
        inertia_src = 'OBSW S20'

    kp = query_s20_float(proc, SRDB_ADCS_KP, 0.5)
    kd = query_s20_float(proc, SRDB_ADCS_KD, 0.1)

    r_orbit = R_EARTH + alt_km * 1e3
    inc_rad = math.radians(inc_deg)

    I     = np.diag(I_diag)
    I_inv = np.linalg.inv(I)

    # ── Initial state ─────────────────────────────────────────────────────
    # Body starts at nadir attitude plus angle_deg mispointing about body z.
    # q = q_nadir(0) ⊗ q_err_0 gives 45° error relative to nadir target.
    q_nadir_0 = nadir_quat(0.0, r_orbit, inc_rad)
    q_err_0   = quat_from_axis_angle([0, 0, 1], math.radians(args.angle_deg))
    q         = quat_multiply(q_nadir_0, q_err_0)
    omega     = np.zeros(3)

    print(f'  alt={alt_km:.0f} km, inc={inc_deg:.1f}°  (orbit source: '
          f'{"CLI" if args.alt_km is not None else "OBSW S20"})')
    print(f'  Inertia: {I_diag} kg·m²  (source: {inertia_src})')
    print(f'  ADCS gains: Kp={kp:.3f}, Kd={kd:.3f}  (from OBSW S20)')
    print(f'  Initial error from nadir: {args.angle_deg:.0f}° about body z-axis')
    print(f'  Target: nadir-pointing LVLH frame (body -z toward Earth)')
    print()
    print(f'Pass criterion: error < {PASS_THRESHOLD_DEG}° within {args.max_t:.0f} s')
    print()

    # ── Transition to NOMINAL via TC(8,1) fid=1 ──────────────────────────
    print('Commanding NOMINAL mode via TC(8,1) fid=1...')
    send_tc(proc, build_tc(APID_DEFAULT, 8, 1, struct.pack('>HB', 1, 0)))
    drain_tc_response(proc)
    print('  Transition complete.')
    print()

    # ── Simulation loop ───────────────────────────────────────────────────
    report_every = max(1, int(10.0 / dt))
    converged_at = None
    step         = 0
    t            = 0.0

    while t <= args.max_t:
        send_sensor(proc, NO_FIELD, q, omega, t,
                    mag_valid=0, st_valid=1, gyro_valid=1)
        act = recv_tick(proc)

        tau   = act.rw_torque
        omega = omega_step(omega, tau, I, I_inv, dt)
        q     = quat_step(q, omega, dt)

        t    += dt
        step += 1

        q_tgt   = nadir_quat(t, r_orbit, inc_rad)
        err_rad = attitude_error_rad(q, q_tgt)
        err_deg = math.degrees(err_rad)

        if step % report_every == 0:
            omega_mag = float(np.linalg.norm(omega))
            print(f'  t={t:7.1f} s  err={err_deg:6.2f}°  '
                  f'|ω|={omega_mag:.4f} rad/s  '
                  f'ctrl={act.controller}')

        if err_rad < PASS_THRESHOLD_RAD:
            converged_at = t
            print(f'\n[PASS] nadir error = {err_deg:.2f}° < {PASS_THRESHOLD_DEG}°'
                  f' at t={t:.1f} s')
            break

    if converged_at is None:
        q_tgt   = nadir_quat(t, r_orbit, inc_rad)
        err_rad = attitude_error_rad(q, q_tgt)
        print(f'\n[FAIL] nadir error={math.degrees(err_rad):.2f}° after {t:.0f} s')
        return 1

    return 0


def main():
    args = parse_args()
    sys.exit(run(args))


if __name__ == '__main__':
    main()
