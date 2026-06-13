#!/usr/bin/env python3
"""
sim/bdot_harness.py — Closed-loop B-dot convergence harness (#67).

Physics model
-------------
- Circular SSO orbit at configurable altitude / inclination
- Geocentric dipole magnetic field (simplified IGRF): dipole aligned with -z
- Rigid-body spacecraft dynamics: Euler equations + quaternion kinematics
- First-order integration at dt=0.1 s (configurable)

Spacecraft configuration is read from the OBSW S20 parameter store via
TC(20,3) at startup (opensvf-kde contract).  CLI arguments override
individual parameters when provided.

Pass criterion
--------------
  |ω| < 0.05 rad/s within 3 orbital periods (~17 190 s at 550 km SSO)

Usage
-----
  source .venv/bin/activate
  python3 sim/bdot_harness.py [--sim PATH] [--dt DT] [--max-periods N]
  bdot-harness          # activate.sh alias
"""

import argparse
import math
import os
import subprocess
import sys

import numpy as np

from wire_proto import (
    SRDB_ORBIT_ALTITUDE_KM, SRDB_ORBIT_INCLINATION_DEG,
    SRDB_SC_INERTIA_XX, SRDB_SC_INERTIA_YY, SRDB_SC_INERTIA_ZZ,
    SRDB_MTQ_MAX_DIPOLE,
    SRDB_SC_OMEGA_X0, SRDB_SC_OMEGA_Y0, SRDB_SC_OMEGA_Z0,
    send_sensor, recv_tick, query_s20_float,
    quat_step, omega_step,
)

# ── Physical constants ────────────────────────────────────────────────────
MU_EARTH = 3.986004418e14   # m³/s²
R_EARTH  = 6.371e6           # m
B0_SURF  = 3.12e-5           # T  geocentric dipole equatorial field

PASS_THRESHOLD = 0.05        # rad/s


# =========================================================================
# Physics
# =========================================================================

def orbit_position(t, r_orbit, inc_rad):
    """ECI position [m] of spacecraft in circular orbit at time t [s]."""
    omega_orb = math.sqrt(MU_EARTH / r_orbit**3)
    nu = omega_orb * t
    return r_orbit * np.array([
        math.cos(nu),
        math.sin(nu) * math.cos(inc_rad),
        math.sin(nu) * math.sin(inc_rad),
    ])


def igrf_dipole_eci(r_eci):
    """
    Geocentric dipole B-field [T] in ECI at position r_eci [m].
    Dipole axis = −z (geographic south → magnetic north pole).
    B = (B₀Rₑ³/r³)(3(m̂·r̂)r̂ − m̂),  m̂ = [0, 0, −1]
    """
    r_mag = np.linalg.norm(r_eci)
    r_hat = r_eci / r_mag
    m_hat = np.array([0.0, 0.0, -1.0])
    scale = B0_SURF * (R_EARTH / r_mag)**3
    return scale * (3.0 * np.dot(m_hat, r_hat) * r_hat - m_hat)


def quat_rot_matrix(q):
    """Rotation matrix for q=[w,x,y,z]. Maps body → ECI: v_eci = R @ v_body."""
    w, x, y, z = q
    return np.array([
        [1 - 2*(y*y + z*z),   2*(x*y - w*z),     2*(x*z + w*y)],
        [    2*(x*y + w*z), 1 - 2*(x*x + z*z),   2*(y*z - w*x)],
        [    2*(x*z - w*y),     2*(y*z + w*x), 1 - 2*(x*x + y*y)],
    ])




# =========================================================================
# CLI
# =========================================================================

def parse_args():
    repo        = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    default_sim = os.path.join(repo, 'build', 'sim', 'obsw_sim')
    p = argparse.ArgumentParser(
        description='B-dot closed-loop convergence harness (openobsw #67)',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument('--sim',         default=default_sim, metavar='PATH',
                   help='path to obsw_sim binary')
    p.add_argument('--dt',          type=float, default=0.1,  metavar='S',
                   help='integration time step [s]')
    p.add_argument('--max-periods', type=float, default=3.0,  metavar='N',
                   help='orbital periods before FAIL')
    # Orbit / spacecraft params — if omitted, values are read from OBSW S20
    p.add_argument('--alt-km',   type=float, default=None, help='orbit altitude [km]')
    p.add_argument('--inc-deg',  type=float, default=None, help='orbit inclination [deg]')
    p.add_argument('--inertia',  nargs=3, type=float, default=None,
                   metavar=('IXX', 'IYY', 'IZZ'), help='principal moments [kg·m²]')
    p.add_argument('--omega0',   nargs=3, type=float, default=None,
                   metavar=('OX', 'OY', 'OZ'), help='initial angular velocity [rad/s]')
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

    # ── Read spacecraft config from OBSW S20 (opensvf-kde contract) ──────
    print('Reading spacecraft config from OBSW S20...')

    alt_km  = query_s20_float(proc, SRDB_ORBIT_ALTITUDE_KM,     550.0) if args.alt_km  is None else args.alt_km
    inc_deg = query_s20_float(proc, SRDB_ORBIT_INCLINATION_DEG,  97.4) if args.inc_deg is None else args.inc_deg

    if args.inertia:
        I_diag = args.inertia
    else:
        I_diag = [
            query_s20_float(proc, SRDB_SC_INERTIA_XX, 2.0e-3),
            query_s20_float(proc, SRDB_SC_INERTIA_YY, 2.5e-3),
            query_s20_float(proc, SRDB_SC_INERTIA_ZZ, 1.5e-3),
        ]

    if args.omega0:
        omega0 = args.omega0
    else:
        omega0 = [
            query_s20_float(proc, SRDB_SC_OMEGA_X0, 0.5),
            query_s20_float(proc, SRDB_SC_OMEGA_Y0, 0.5),
            query_s20_float(proc, SRDB_SC_OMEGA_Z0, 0.5),
        ]

    # ── Derived orbital quantities ────────────────────────────────────────
    r_orbit = R_EARTH + alt_km * 1e3
    T_orbit = 2.0 * math.pi * math.sqrt(r_orbit**3 / MU_EARTH)
    inc_rad = math.radians(inc_deg)
    max_t   = args.max_periods * T_orbit

    I     = np.diag(I_diag)
    I_inv = np.linalg.inv(I)
    omega = np.array(omega0, dtype=float)
    q     = np.array([1.0, 0.0, 0.0, 0.0])   # body aligned with ECI at t=0

    print(f'  alt={alt_km:.0f} km, inc={inc_deg}°, T={T_orbit:.0f} s'
          f'  (config source: {"CLI" if args.alt_km is not None else "OBSW S20"})')
    print(f'  I = {I_diag} kg·m²')
    print(f'  ω₀ = {omega} rad/s  |ω|={np.linalg.norm(omega):.3f} rad/s')
    print()
    print(f'Target:  |ω| < {PASS_THRESHOLD} rad/s within {args.max_periods:.0f} periods'
          f' ({max_t:.0f} s)')
    print()

    report_every = max(1, int(100.0 / dt))
    converged_at = None
    step         = 0
    t            = 0.0

    while t <= max_t:
        r_eci  = orbit_position(t, r_orbit, inc_rad)
        B_eci  = igrf_dipole_eci(r_eci)
        R      = quat_rot_matrix(q)
        B_body = R.T @ B_eci

        send_sensor(proc, B_body, q, omega, t)
        act = recv_tick(proc)

        tau   = np.cross(act.mtq, B_body)   # τ = m × B [N·m]
        omega = omega_step(omega, tau, I, I_inv, dt)
        q     = quat_step(q, omega, dt)

        t    += dt
        step += 1

        omega_mag = float(np.linalg.norm(omega))

        if step % report_every == 0:
            print(f'  t={t:8.1f} s ({t/T_orbit:.2f}P)  '
                  f'|ω|={omega_mag:.4f} rad/s  '
                  f'|m|={float(np.linalg.norm(act.mtq)):.3e} Am²')

        if omega_mag < PASS_THRESHOLD:
            converged_at = t
            print(f'\n[PASS] |ω|={omega_mag:.4f} rad/s < {PASS_THRESHOLD} rad/s '
                  f'at t={t:.1f} s ({t/T_orbit:.2f} orbital periods)')
            break

    if converged_at is None:
        omega_mag = float(np.linalg.norm(omega))
        print(f'\n[FAIL] |ω|={omega_mag:.4f} rad/s after {t:.0f} s '
              f'({t/T_orbit:.2f} orbital periods)')
        return 1

    return 0


def main():
    args = parse_args()
    sys.exit(run(args))


if __name__ == '__main__':
    main()
