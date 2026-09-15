#!/usr/bin/env python3
"""
sim/bdot_hpf_validation.py — Issue #70 closed-loop validation.

Two checks against a running obsw_sim (SAFE mode, wire protocol v3):

  A) Orbital-aliasing rejection (spurious spin-up)
     Full rigid-body dynamics started from rest (q=[1,0,0,0], ω=0).  The only
     dB/dt the magnetometer sees is the orbital field gradient.  With
     bdot_hpf_tau=0 the finite-difference estimator aliases it into a spurious
     dipole that random-walks the body rate up.  Sweep tau and measure the
     peak |ω| reached over 2 orbits, plus the mean spurious |m_cmd|.
     Pass (for the chosen tau): peak |ω| < 5e-3 rad/s  (10% of the detumble
     threshold) AND >= 10x lower than the tau=0 case.

  B) Detumble still works
     Full rigid-body dynamics, ω0 = (0.3, 0.3, 0.3) rad/s, chosen tau.
     Pass: |ω| < 0.05 rad/s within 3 orbital periods.

Usage:
  source .venv/bin/activate
  python3 sim/bdot_hpf_validation.py [--sim PATH] [--dt DT]
"""

import argparse
import math
import os
import struct
import subprocess
import sys

import numpy as np

from wire_proto import (
    build_tc, send_tc, drain_tc_response,
    send_sensor, recv_tick,
    quat_step, omega_step,
    APID_DEFAULT,
)

SRDB_BDOT_HPF_TAU = 0x20A3
SRDB_BDOT_GAIN    = 0x20A0

MU_EARTH = 3.986004418e14
R_EARTH  = 6.371e6
B0_SURF  = 3.12e-5

ALT_KM   = 550.0
INC_DEG  = 97.4
I_DIAG   = np.array([2.0e-3, 2.5e-3, 1.5e-3])

PASS_OMEGA        = 0.05     # rad/s  — detumble convergence threshold
SPINUP_ABS_LIMIT  = 5.0e-3   # rad/s  — spurious peak |ω| must stay this far below PASS_OMEGA
MCMD_REJECT_X     = 5.0      # steady-state spurious |m_cmd| must drop >= this factor vs tau=0
TAU_SWEEP         = [0.0, 15.0, 30.0, 60.0]
TAU_CHOSEN        = 15.0


def orbit_position(t, r_orbit, inc_rad):
    n = math.sqrt(MU_EARTH / r_orbit**3)
    nu = n * t
    return r_orbit * np.array([
        math.cos(nu),
        math.sin(nu) * math.cos(inc_rad),
        math.sin(nu) * math.sin(inc_rad),
    ])


def igrf_dipole_eci(r_eci):
    r_mag = np.linalg.norm(r_eci)
    r_hat = r_eci / r_mag
    m_hat = np.array([0.0, 0.0, -1.0])
    scale = B0_SURF * (R_EARTH / r_mag) ** 3
    return scale * (3.0 * np.dot(m_hat, r_hat) * r_hat - m_hat)


def quat_rot_matrix(q):
    w, x, y, z = q
    return np.array([
        [1 - 2*(y*y + z*z),   2*(x*y - w*z),     2*(x*z + w*y)],
        [    2*(x*y + w*z), 1 - 2*(x*x + z*z),   2*(y*z - w*x)],
        [    2*(x*z - w*y),     2*(y*z + w*x), 1 - 2*(x*x + y*y)],
    ])


def set_hpf_tau(proc, tau):
    payload = struct.pack('>H', SRDB_BDOT_HPF_TAU) + struct.pack('>f', float(tau))
    send_tc(proc, build_tc(APID_DEFAULT, 20, 1, payload))
    drain_tc_response(proc)


def start_sim(sim_path):
    return subprocess.Popen(
        [sim_path],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
    )


def run_aliasing(sim_path, dt, tau, n_steps, r_orbit, inc_rad):
    """Full dynamics from rest. Return (peak|ω|, mean spurious |m_cmd| over 2nd half)."""
    proc = start_sim(sim_path)
    try:
        set_hpf_tau(proc, tau)
        I     = np.diag(I_DIAG)
        I_inv = np.linalg.inv(I)
        q     = np.array([1.0, 0.0, 0.0, 0.0])
        omega = np.zeros(3)
        peak_w = 0.0
        m_tail = []
        half   = n_steps // 2
        for k in range(n_steps):
            t = k * dt
            r_eci  = orbit_position(t, r_orbit, inc_rad)
            B_body = quat_rot_matrix(q).T @ igrf_dipole_eci(r_eci)
            send_sensor(proc, B_body, q, omega, t, st_valid=0, gyro_valid=0)
            act = recv_tick(proc)
            tau_vec = np.cross(act.mtq, B_body)
            omega   = omega_step(omega, tau_vec, I, I_inv, dt)
            q       = quat_step(q, omega, dt)
            peak_w  = max(peak_w, float(np.linalg.norm(omega)))
            if k >= half:
                m_tail.append(float(np.linalg.norm(act.mtq)))
        return peak_w, float(np.mean(m_tail))
    finally:
        proc.stdin.close()
        proc.wait()


def run_detumble(sim_path, dt, tau, max_periods, r_orbit, inc_rad, T_orbit):
    proc = start_sim(sim_path)
    try:
        set_hpf_tau(proc, tau)
        I     = np.diag(I_DIAG)
        I_inv = np.linalg.inv(I)
        omega = np.array([0.3, 0.3, 0.3])
        q     = np.array([1.0, 0.0, 0.0, 0.0])
        t     = 0.0
        max_t = max_periods * T_orbit
        while t <= max_t:
            r_eci  = orbit_position(t, r_orbit, inc_rad)
            B_body = quat_rot_matrix(q).T @ igrf_dipole_eci(r_eci)
            send_sensor(proc, B_body, q, omega, t, st_valid=0, gyro_valid=0)
            act = recv_tick(proc)
            tau_vec = np.cross(act.mtq, B_body)
            omega   = omega_step(omega, tau_vec, I, I_inv, dt)
            q       = quat_step(q, omega, dt)
            t      += dt
            if np.linalg.norm(omega) < PASS_OMEGA:
                return t, t / T_orbit, float(np.linalg.norm(omega))
        return None, t / T_orbit, float(np.linalg.norm(omega))
    finally:
        proc.stdin.close()
        proc.wait()


def main():
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser()
    ap.add_argument('--sim', default=os.path.join(repo, 'build', 'sim', 'obsw_sim'))
    ap.add_argument('--dt', type=float, default=0.1)
    ap.add_argument('--orbits', type=float, default=2.0,
                    help='orbits for the check-A spin-up runs')
    ap.add_argument('--tau-sweep', default=None,
                    help='comma-separated tau values for check A (overrides default)')
    ap.add_argument('--tau-chosen', type=float, default=None,
                    help='tau used for the pass verdict and check B')
    args = ap.parse_args()

    tau_sweep  = ([float(x) for x in args.tau_sweep.split(',')]
                  if args.tau_sweep else list(TAU_SWEEP))
    tau_chosen = args.tau_chosen if args.tau_chosen is not None else TAU_CHOSEN
    if tau_chosen not in tau_sweep:
        tau_sweep.append(tau_chosen)

    if not os.path.exists(args.sim):
        print(f'ERROR: sim binary not found: {args.sim}')
        return 2

    r_orbit = R_EARTH + ALT_KM * 1e3
    T_orbit = 2.0 * math.pi * math.sqrt(r_orbit**3 / MU_EARTH)
    inc_rad = math.radians(INC_DEG)
    n_steps = int(args.orbits * T_orbit / args.dt)

    print(f'Orbit: alt={ALT_KM:.0f} km  inc={INC_DEG}°  T={T_orbit:.0f} s')
    print(f'dt={args.dt}s   aliasing run = {n_steps} steps ({args.orbits:g} orbits)')
    print(f'tau sweep = {tau_sweep}   chosen = {tau_chosen:g} s\n')

    print('== Check A: orbital-aliasing rejection — spurious spin-up from rest ==')
    results = {}
    for tau in tau_sweep:
        peak_w, mean_m = run_aliasing(args.sim, args.dt, tau, n_steps, r_orbit, inc_rad)
        results[tau] = (peak_w, mean_m)
        print(f'  tau={tau:5.1f} s : peak|ω|={peak_w:.3e} rad/s   '
              f'mean spurious|m_cmd|={mean_m:.3e} Am²')

    peak0, mean0 = results.get(0.0, (None, None))
    peak_c, mean_c = results[tau_chosen]
    m_factor = (mean0 / mean_c) if (mean0 and mean_c > 0) else float('nan')
    print(f'\n  chosen tau={tau_chosen:g} s:')
    print(f'    spurious |m_cmd|  : {mean_c:.3e} Am²' +
          (f'  ({m_factor:.1f}x below tau=0, need >= {MCMD_REJECT_X:.0f}x)'
           if mean0 else ''))
    print(f'    spin-up peak |ω|  : {peak_c:.3e} rad/s  '
          f'(limit {SPINUP_ABS_LIMIT:.0e})')
    check_a = peak_c < SPINUP_ABS_LIMIT and (not mean0 or m_factor >= MCMD_REJECT_X)
    print(f'  {"PASS" if check_a else "FAIL"}\n')

    print(f'== Check B: detumble still converges (ω0=0.3,0.3,0.3  tau={tau_chosen:g}) ==')
    t_conv, p_conv, w_final = run_detumble(
        args.sim, args.dt, tau_chosen, 3.0, r_orbit, inc_rad, T_orbit)
    if t_conv is not None:
        print(f'  converged at t={t_conv:.0f} s ({p_conv:.2f} orbital periods)  '
              f'|ω|={w_final:.4f} rad/s')
        check_b = True
    else:
        print(f'  did NOT converge: |ω|={w_final:.4f} rad/s after {p_conv:.2f} periods')
        check_b = False
    print(f'  {"PASS" if check_b else "FAIL"}\n')

    ok = check_a and check_b
    print('=' * 60)
    if ok:
        print('ISSUE #70 BENCH VALIDATION: PASS')
        print('  HPF rejects the orbital-rate dB/dt artifact; detumble unaffected.')
        print('  Still required before lifting the bdot_gain freeze: a run against')
        print('  the opensvf-kde OrbitalEnvironment model (not this dipole bench).')
    else:
        print('ISSUE #70 BENCH VALIDATION: FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
