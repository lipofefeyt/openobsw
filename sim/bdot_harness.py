#!/usr/bin/env python3
"""
sim/bdot_harness.py — Closed-loop B-dot convergence harness (#67).

Physics model
-------------
- Circular SSO orbit at 550 km, inclination 97.4 deg
- Geocentric dipole magnetic field (simplified IGRF): dipole aligned with -z
- Rigid-body spacecraft dynamics: Euler equations + quaternion kinematics
- First-order integration (Euler step) at dt=0.1 s

The OBSW host sim is launched as a subprocess. It starts in SAFE mode with
B-dot active. The harness drives it with synthetic sensor frames (type 0x02)
and reads back magnetorquer dipole commands (type 0x03). MTQ torque
τ = m_cmd × B is applied and the angular velocity integrated.

Pass criterion
--------------
  |ω| < 0.05 rad/s within 3 orbital periods (~17 190 s)

Usage
-----
  source .venv/bin/activate
  python3 sim/bdot_harness.py [--sim PATH] [--dt DT] [--max-periods N]
"""

import argparse
import math
import os
import struct
import subprocess
import sys

import numpy as np

# ── Physical constants ────────────────────────────────────────────────────
MU_EARTH = 3.986004418e14   # m³/s²
R_EARTH  = 6.371e6           # m
B0_SURF  = 3.12e-5           # T  geocentric dipole equatorial field at surface

# ── Spacecraft defaults (match SRDB 0x30xx) ───────────────────────────────
DEFAULT_ALT_KM   = 550.0
DEFAULT_INC_DEG  = 97.4
DEFAULT_I_DIAG   = [2.0e-3, 2.5e-3, 1.5e-3]   # kg·m²  (principal axes)
DEFAULT_OMEGA_0  = [0.5, 0.5, 0.5]             # rad/s  initial tumble

PASS_THRESHOLD   = 0.05   # rad/s

# ── Wire protocol v3 ──────────────────────────────────────────────────────
FRAME_TC     = 0x01
FRAME_SENSOR = 0x02
FRAME_ACT    = 0x03
FRAME_TM     = 0x04
SYNC_EOT     = 0xFF

SENSOR_LEN = 47
ACTUAT_LEN = 29

# struct '<fffBffffBfffBf' == 47 bytes
SENSOR_FMT = '<fffBffffBfffBf'
# struct '<ffffffBf' == 29 bytes
ACTUAT_FMT = '<ffffffBf'


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
    Dipole axis = -z (geographic south pole → magnetic north pole).
    B = (B0*Re³/r³)(3(m̂·r̂)r̂ − m̂),  m̂ = [0,0,−1]
    """
    r_mag = np.linalg.norm(r_eci)
    r_hat = r_eci / r_mag
    m_hat = np.array([0.0, 0.0, -1.0])
    scale = B0_SURF * (R_EARTH / r_mag)**3
    return scale * (3.0 * np.dot(m_hat, r_hat) * r_hat - m_hat)


def quat_rot_matrix(q):
    """
    Rotation matrix for unit quaternion q = [w, x, y, z].
    Maps body frame → ECI: v_eci = R @ v_body.
    """
    w, x, y, z = q
    return np.array([
        [1 - 2*(y*y + z*z),   2*(x*y - w*z),     2*(x*z + w*y)],
        [    2*(x*y + w*z), 1 - 2*(x*x + z*z),   2*(y*z - w*x)],
        [    2*(x*z - w*y),     2*(y*z + w*x), 1 - 2*(x*x + y*y)],
    ])


def quat_step(q, omega, dt):
    """
    First-order quaternion kinematic update.
    q = body→ECI quaternion, omega in body frame [rad/s].
    q̇ = ½ q ⊗ [0, ω]
    """
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
    """
    Euler rigid-body integration step.
    I·ω̇ = τ − ω × (I·ω)   →   ω_new = ω + dt·I⁻¹(τ − ω×(I·ω))
    """
    I_omega = I @ omega
    domega  = I_inv @ (torque - np.cross(omega, I_omega))
    return omega + dt * domega


# =========================================================================
# Wire protocol helpers
# =========================================================================

def _read_exact(f, n):
    """Blocking read of exactly n bytes from file-like f."""
    data = b''
    while len(data) < n:
        chunk = f.read(n - len(data))
        if not chunk:
            raise EOFError('sim stdout closed unexpectedly')
        data += chunk
    return data


def send_sensor(proc, B_body, q, omega, t):
    """Pack and send a type-0x02 sensor frame to the sim subprocess."""
    payload = struct.pack(SENSOR_FMT,
        float(B_body[0]), float(B_body[1]), float(B_body[2]), 1,   # mag + valid
        float(q[0]),      float(q[1]),      float(q[2]),      float(q[3]), 1,  # ST + valid
        float(omega[0]),  float(omega[1]),  float(omega[2]),  1,   # gyro + valid
        float(t),
    )
    proc.stdin.write(bytes([FRAME_SENSOR]) + struct.pack('>H', len(payload)) + payload)
    proc.stdin.flush()


def recv_tick(proc):
    """
    Drain one complete tick from sim stdout (up to and including 0xFF EOT).
    Returns (mtq_dipole [Am²], controller_id [int]).
    TM packets (0x04) are discarded; the actuator frame (0x03) is parsed.
    """
    mtq  = np.zeros(3)
    ctrl = 0
    while True:
        ftype = _read_exact(proc.stdout, 1)[0]
        if ftype == SYNC_EOT:
            return mtq, ctrl
        flen    = struct.unpack('>H', _read_exact(proc.stdout, 2))[0]
        payload = _read_exact(proc.stdout, flen)
        if ftype == FRAME_ACT and len(payload) == ACTUAT_LEN:
            vals = struct.unpack(ACTUAT_FMT, payload)
            mtq  = np.array([vals[0], vals[1], vals[2]])
            ctrl = vals[6]
        # FRAME_TM and unknowns: payload already consumed, continue


# =========================================================================
# Main harness
# =========================================================================

def parse_args():
    repo        = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    default_sim = os.path.join(repo, 'build', 'sim', 'obsw_sim')
    p = argparse.ArgumentParser(
        description='B-dot closed-loop convergence harness (openobsw #67)',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument('--sim',        default=default_sim, metavar='PATH',
                   help='path to obsw_sim binary')
    p.add_argument('--dt',         type=float, default=0.1,
                   metavar='S',    help='integration time step [s]')
    p.add_argument('--max-periods', type=float, default=3.0,
                   metavar='N',    help='orbital periods before FAIL')
    p.add_argument('--omega0',     nargs=3, type=float, default=DEFAULT_OMEGA_0,
                   metavar=('OX', 'OY', 'OZ'),
                   help='initial angular velocity [rad/s] (body frame)')
    p.add_argument('--alt-km',     type=float, default=DEFAULT_ALT_KM,
                   help='orbit altitude [km]')
    p.add_argument('--inc-deg',    type=float, default=DEFAULT_INC_DEG,
                   help='orbit inclination [deg]')
    p.add_argument('--inertia',    nargs=3, type=float, default=DEFAULT_I_DIAG,
                   metavar=('IXX', 'IYY', 'IZZ'),
                   help='principal moments of inertia [kg·m²]')
    return p.parse_args()


def run(args):
    r_orbit  = R_EARTH + args.alt_km * 1e3
    T_orbit  = 2.0 * math.pi * math.sqrt(r_orbit**3 / MU_EARTH)
    inc_rad  = math.radians(args.inc_deg)
    max_t    = args.max_periods * T_orbit
    dt       = args.dt

    I     = np.diag(args.inertia)
    I_inv = np.linalg.inv(I)

    omega = np.array(args.omega0, dtype=float)
    # Initial attitude: body frame aligned with ECI
    q     = np.array([1.0, 0.0, 0.0, 0.0])

    print('=== B-dot convergence harness ===')
    print(f'Orbit:    alt={args.alt_km:.0f} km, i={args.inc_deg}°, T={T_orbit:.0f} s')
    print(f'Inertia:  {args.inertia} kg·m²')
    print(f'ω₀:       {omega} rad/s  (|ω|={np.linalg.norm(omega):.3f} rad/s)')
    print(f'Target:   |ω| < {PASS_THRESHOLD} rad/s within {args.max_periods:.0f} periods '
          f'({max_t:.0f} s)')
    print()

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

    report_every = max(1, int(100.0 / dt))   # console update every 100 s of sim-time
    converged_at = None
    step         = 0
    t            = 0.0

    try:
        while t <= max_t:
            # ── Compute B in body frame ───────────────────────────────────
            r_eci  = orbit_position(t, r_orbit, inc_rad)
            B_eci  = igrf_dipole_eci(r_eci)
            R      = quat_rot_matrix(q)
            B_body = R.T @ B_eci           # v_body = R^T @ v_eci

            # ── Drive sim, receive MTQ command ────────────────────────────
            send_sensor(proc, B_body, q, omega, t)
            mtq, _ctrl = recv_tick(proc)

            # ── Apply MTQ torque and integrate dynamics ───────────────────
            tau   = np.cross(mtq, B_body)  # τ = m × B [N·m], body frame
            omega = omega_step(omega, tau, I, I_inv, dt)
            q     = quat_step(q, omega, dt)

            t    += dt
            step += 1

            omega_mag = float(np.linalg.norm(omega))

            if step % report_every == 0:
                print(f'  t={t:8.1f} s ({t/T_orbit:.2f}P)  '
                      f'|ω|={omega_mag:.4f} rad/s  '
                      f'|m|={float(np.linalg.norm(mtq)):.3e} Am²')

            if omega_mag < PASS_THRESHOLD:
                converged_at = t
                print(f'\n[PASS] |ω|={omega_mag:.4f} rad/s < {PASS_THRESHOLD} rad/s '
                      f'at t={t:.1f} s ({t/T_orbit:.2f} orbital periods)')
                break

    except (EOFError, BrokenPipeError) as exc:
        print(f'\nERROR: sim process died unexpectedly: {exc}')
        return 1
    finally:
        try:
            proc.stdin.close()
        except OSError:
            pass
        proc.wait()

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
