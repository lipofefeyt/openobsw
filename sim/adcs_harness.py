#!/usr/bin/env python3
"""
sim/adcs_harness.py — Closed-loop PD ADCS (NOMINAL mode) convergence harness.

Tests that the proportional-derivative attitude controller drives the
spacecraft from an initial pointing error to within 2° of the target
attitude (identity quaternion) within 120 s.

Physics
-------
No orbit propagation is needed — only rigid-body attitude dynamics:

  I·ω̇ = τ_rw − ω × (I·ω)      (Euler equations)
  q̇   = ½ q ⊗ [0, ω]          (quaternion kinematics)

where τ_rw are the reaction wheel torque commands from the OBSW.

The ADCS target is always the identity quaternion [1, 0, 0, 0] (hard-coded
default in adcs.c). Attitude error angle = 2·arccos(|q_meas.w|).

Spacecraft config is read from OBSW S20 via TC(20,3) at startup.

Pass criterion
--------------
  attitude_error < 2° within 120 s

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
    build_tc, send_tc, drain_tc_response,
    send_sensor, recv_tick, query_s20_float,
    APID_DEFAULT,
)

PASS_THRESHOLD_DEG = 2.0     # degrees
PASS_THRESHOLD_RAD = math.radians(PASS_THRESHOLD_DEG)

NO_FIELD = np.zeros(3)       # mag_valid=0 in NOMINAL; ADCS uses ST+gyro only


# =========================================================================
# Attitude math
# =========================================================================

def quat_from_axis_angle(axis, angle_rad):
    """Unit quaternion [w, x, y, z] for rotation of angle_rad about axis."""
    axis = np.array(axis, dtype=float)
    axis = axis / np.linalg.norm(axis)
    s = math.sin(angle_rad / 2.0)
    return np.array([math.cos(angle_rad / 2.0),
                     axis[0]*s, axis[1]*s, axis[2]*s])


def attitude_error_rad(q):
    """
    Attitude error angle [rad] when target is identity [1,0,0,0].
    q = [w, x, y, z] body→ECI.  Error quaternion = identity ⊗ q* = q*.
    Short-path: use |q.w|.
    """
    return 2.0 * math.acos(min(1.0, abs(float(q[0]))))


def quat_step(q, omega, dt):
    """First-order quaternion kinematic update. q̇ = ½ q ⊗ [0, ω]."""
    w, x, y, z = q
    ox, oy, oz = omega
    dq = 0.5 * np.array([
        -x*ox - y*oy - z*oz,
         w*ox + y*oz - z*oy,
         w*oy - x*oz + z*ox,
         w*oz + x*oy - y*ox,
    ])
    q_new = q + dt * dq
    return q_new / np.linalg.norm(q_new)


def omega_step(omega, torque, I, I_inv, dt):
    """Euler rigid-body step. I·ω̇ = τ − ω × (I·ω)."""
    I_omega = I @ omega
    return omega + dt * (I_inv @ (torque - np.cross(omega, I_omega)))


# =========================================================================
# CLI
# =========================================================================

def parse_args():
    repo        = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    default_sim = os.path.join(repo, 'build', 'sim', 'obsw_sim')
    p = argparse.ArgumentParser(
        description='PD ADCS closed-loop convergence harness (NOMINAL mode)',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument('--sim',       default=default_sim, metavar='PATH',
                   help='path to obsw_sim binary')
    p.add_argument('--dt',        type=float, default=0.01,  metavar='S',
                   help='integration time step [s] — must be < 0.033 s for Euler stability')
    p.add_argument('--max-t',     type=float, default=120.0, metavar='S',
                   help='max simulation time before FAIL [s]')
    p.add_argument('--angle-deg', type=float, default=45.0,
                   help='initial attitude error angle about z-axis [deg]')
    p.add_argument('--inertia',   nargs=3, type=float, default=None,
                   metavar=('IXX', 'IYY', 'IZZ'),
                   help='principal moments [kg·m²] (default: read from OBSW)')
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

    if args.inertia:
        I_diag = args.inertia
        src = 'CLI'
    else:
        I_diag = [
            query_s20_float(proc, SRDB_SC_INERTIA_XX, 2.0e-3),
            query_s20_float(proc, SRDB_SC_INERTIA_YY, 2.5e-3),
            query_s20_float(proc, SRDB_SC_INERTIA_ZZ, 1.5e-3),
        ]
        src = 'OBSW S20'

    kp = query_s20_float(proc, SRDB_ADCS_KP, 0.5)
    kd = query_s20_float(proc, SRDB_ADCS_KD, 0.1)

    I     = np.diag(I_diag)
    I_inv = np.linalg.inv(I)

    # ── Initial state ─────────────────────────────────────────────────────
    # Body is rotated args.angle_deg about z-axis relative to ECI
    angle_0 = math.radians(args.angle_deg)
    q     = quat_from_axis_angle([0, 0, 1], angle_0)
    omega = np.zeros(3)

    print(f'  Inertia: {I_diag} kg·m²  (source: {src})')
    print(f'  ADCS gains: Kp={kp:.3f}, Kd={kd:.3f}  (from OBSW S20)')
    print(f'  Initial attitude error: {args.angle_deg:.0f}° about z-axis')
    print(f'  Target: identity quaternion [1,0,0,0]')
    print()
    print(f'Target: error < {PASS_THRESHOLD_DEG}° within {args.max_t:.0f} s')
    print()

    # ── Transition to NOMINAL via TC(8,1) fid=1 ──────────────────────────
    print('Commanding NOMINAL mode via TC(8,1) fid=1...')
    send_tc(proc, build_tc(APID_DEFAULT, 8, 1, struct.pack('>HB', 1, 0)))
    drain_tc_response(proc)
    print('  Transition complete.')
    print()

    # ── Simulation loop ───────────────────────────────────────────────────
    report_every = max(1, int(10.0 / dt))   # report every 10 s
    converged_at = None
    step         = 0
    t            = 0.0

    while t <= args.max_t:
        # Star tracker reports current attitude; gyro reports current ω.
        # Magnetometer not used in NOMINAL (mag_valid=0).
        send_sensor(proc, NO_FIELD, q, omega, t,
                    mag_valid=0, st_valid=1, gyro_valid=1)
        act = recv_tick(proc)

        if act.controller != 1 and step == 0:
            print('  WARNING: controller=0 (B-dot) on first tick — '
                  'NOMINAL transition may have failed')

        # Apply reaction wheel torques to spacecraft body
        tau   = act.rw_torque
        omega = omega_step(omega, tau, I, I_inv, dt)
        q     = quat_step(q, omega, dt)

        t    += dt
        step += 1

        err_rad = attitude_error_rad(q)
        err_deg = math.degrees(err_rad)

        if step % report_every == 0:
            omega_mag = float(np.linalg.norm(omega))
            print(f'  t={t:7.1f} s  err={err_deg:6.2f}°  '
                  f'|ω|={omega_mag:.4f} rad/s  '
                  f'ctrl={act.controller}')

        if err_rad < PASS_THRESHOLD_RAD:
            converged_at = t
            print(f'\n[PASS] attitude error = {err_deg:.2f}° < {PASS_THRESHOLD_DEG}°'
                  f' at t={t:.1f} s')
            break

    if converged_at is None:
        err_rad = attitude_error_rad(q)
        print(f'\n[FAIL] error={math.degrees(err_rad):.2f}° after {t:.0f} s')
        return 1

    return 0


def main():
    args = parse_args()
    sys.exit(run(args))


if __name__ == '__main__':
    main()
