#!/usr/bin/env python3
"""
sim/demo.py — Live ADCS nadir-pointing convergence demo.

Opens a real-time matplotlib window showing attitude error converging to
nadir as the openobsw PD controller runs.  Designed to be screen-recorded.

Usage
-----
  source .venv/bin/activate
  python3 sim/demo.py [--save adcs_convergence.png]

Requirements: matplotlib  (pip install matplotlib)
"""

import math
import os
import struct
import subprocess
import sys

import numpy as np
import matplotlib.pyplot as plt

# ── Import physics helpers from the test harness (harness itself stays clean) ──
_SIM_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _SIM_DIR)

from adcs_harness import (                          # noqa: E402
    nadir_quat, attitude_error_rad, quat_from_axis_angle,
    quat_multiply, PASS_THRESHOLD_DEG, PASS_THRESHOLD_RAD,
    MU_EARTH, R_EARTH,
)
from wire_proto import (                            # noqa: E402
    SRDB_SC_INERTIA_XX, SRDB_SC_INERTIA_YY, SRDB_SC_INERTIA_ZZ,
    SRDB_ADCS_KP, SRDB_ADCS_KD,
    SRDB_ORBIT_ALTITUDE_KM, SRDB_ORBIT_INCLINATION_DEG,
    build_tc, send_tc, drain_tc_response,
    send_sensor, recv_tick, query_s20_float,
    quat_step, omega_step,
    APID_DEFAULT,
)

NO_FIELD = np.zeros(3)

# GitHub-dark palette
BG       = '#0d1117'
AXES_BG  = '#0d1117'
GRID     = '#21262d'
SPINE    = '#30363d'
FG       = '#c9d1d9'
TITLE_FG = '#e6edf3'
BLUE     = '#58a6ff'
RED      = '#f85149'
GREEN    = '#3fb950'
MONO     = 'monospace'


# ── CLI ───────────────────────────────────────────────────────────────────────

def _parse():
    import argparse
    repo = os.path.dirname(_SIM_DIR)
    p = argparse.ArgumentParser(
        description='Live ADCS nadir-pointing convergence demo',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument('--sim',        default=os.path.join(repo, 'build', 'sim', 'obsw_sim'),
                   metavar='PATH', help='obsw_sim binary')
    p.add_argument('--dt',         type=float, default=0.01,  metavar='S',
                   help='integration time step')
    p.add_argument('--angle-deg',  type=float, default=45.0,
                   help='initial attitude error from nadir [deg]')
    p.add_argument('--step-delay', type=float, default=0.04,  metavar='S',
                   help='pause between steps — controls animation speed')
    p.add_argument('--save',       metavar='PATH', default=None,
                   help='also save final plot to this PNG path')
    return p.parse_args()


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    args = _parse()

    if not os.path.exists(args.sim):
        print(f'ERROR: sim binary not found: {args.sim}')
        print('  Build first:  cmake --build build -j$(nproc)')
        sys.exit(2)

    proc = subprocess.Popen(
        [args.sim],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
    )
    try:
        rc = _run(args, proc)
    except (EOFError, BrokenPipeError) as exc:
        print(f'\nERROR: sim process died: {exc}')
        rc = 1
    finally:
        try:
            proc.stdin.close()
        except OSError:
            pass
        proc.wait()
    sys.exit(rc)


def _run(args, proc):
    dt = args.dt

    # ── Query spacecraft config ───────────────────────────────────────────
    print('openobsw  ·  ADCS nadir-pointing demo')
    print('─' * 42)
    print('Querying S20 parameter store...')

    alt_km  = query_s20_float(proc, SRDB_ORBIT_ALTITUDE_KM,     550.0)
    inc_deg = query_s20_float(proc, SRDB_ORBIT_INCLINATION_DEG,  97.4)
    I_diag  = [
        query_s20_float(proc, SRDB_SC_INERTIA_XX, 2.0e-3),
        query_s20_float(proc, SRDB_SC_INERTIA_YY, 2.5e-3),
        query_s20_float(proc, SRDB_SC_INERTIA_ZZ, 1.5e-3),
    ]
    kp = query_s20_float(proc, SRDB_ADCS_KP, 0.5)
    kd = query_s20_float(proc, SRDB_ADCS_KD, 0.1)

    r_orbit = R_EARTH + alt_km * 1e3
    inc_rad = math.radians(inc_deg)
    I       = np.diag(I_diag)
    I_inv   = np.linalg.inv(I)

    print(f'  Orbit  : {alt_km:.0f} km  ·  {inc_deg:.1f}° inclination')
    print(f'  ADCS   : Kp={kp:.2f}  Kd={kd:.2f}')
    print(f'  Error₀ : {args.angle_deg:.0f}° from nadir (about body z)')
    print()

    # ── Initial attitude state ────────────────────────────────────────────
    q_nadir_0 = nadir_quat(0.0, r_orbit, inc_rad)
    q_err_0   = quat_from_axis_angle([0, 0, 1], math.radians(args.angle_deg))
    q         = quat_multiply(q_nadir_0, q_err_0)
    omega     = np.zeros(3)

    # ── Transition to NOMINAL ─────────────────────────────────────────────
    print('Commanding NOMINAL mode via TC(8,1)...')
    send_tc(proc, build_tc(APID_DEFAULT, 8, 1, struct.pack('>HB', 1, 0)))
    drain_tc_response(proc)
    print('  Ready.\n')

    # ── Live plot ─────────────────────────────────────────────────────────
    plt.ion()
    fig, ax = plt.subplots(figsize=(11, 6))
    fig.patch.set_facecolor(BG)
    ax.set_facecolor(AXES_BG)

    line, = ax.plot([], [], color=BLUE, linewidth=2.2, label='Nadir pointing error')
    ax.axhline(PASS_THRESHOLD_DEG, color=RED, linestyle='--', linewidth=1.5,
               label=f'{PASS_THRESHOLD_DEG}° pass threshold')

    ax.set_xlim(0, 5.0)
    ax.set_ylim(0, args.angle_deg * 1.15)
    ax.set_xlabel('Simulation time  [s]',          color=FG,       fontsize=13)
    ax.set_ylabel('Nadir pointing error  [°]',     color=FG,       fontsize=13)
    ax.set_title(
        'openobsw  —  ADCS Nadir-Pointing Convergence\n'
        'PD quaternion controller  ·  550 km SSO  ·  real OBSW binary in the loop',
        color=TITLE_FG, fontsize=13, pad=12,
    )
    ax.tick_params(colors='#8b949e', labelsize=11)
    for sp in ax.spines.values():
        sp.set_edgecolor(SPINE)
    ax.grid(True, color=GRID, linewidth=0.8)
    ax.legend(loc='upper right', fontsize=11,
              facecolor='#161b22', edgecolor=SPINE, labelcolor=FG)

    # Live readout: current error value
    readout = ax.text(0.02, 0.93, f'error = {args.angle_deg:.1f}°',
                      transform=ax.transAxes, color=BLUE,
                      fontsize=14, va='top', fontfamily=MONO)

    fig.tight_layout()
    plt.pause(0.3)

    # ── Simulation loop ───────────────────────────────────────────────────
    history_t   = []
    history_err = []
    converged_at = None
    t = 0.0

    print(f'Simulating  (step delay {args.step_delay*1000:.0f} ms)...')

    while True:
        send_sensor(proc, NO_FIELD, q, omega, t,
                    mag_valid=0, st_valid=1, gyro_valid=1)
        act = recv_tick(proc)

        omega = omega_step(omega, act.rw_torque, I, I_inv, dt)
        q     = quat_step(q, omega, dt)
        t    += dt

        q_tgt   = nadir_quat(t, r_orbit, inc_rad)
        err_rad = attitude_error_rad(q, q_tgt)
        err_deg = math.degrees(err_rad)

        history_t.append(t)
        history_err.append(err_deg)

        line.set_data(history_t, history_err)
        readout.set_text(f'error = {err_deg:5.2f}°')
        plt.pause(args.step_delay)

        if converged_at is None and err_rad < PASS_THRESHOLD_RAD:
            converged_at = t
            print(f'[PASS]  nadir error = {err_deg:.2f}°  at t = {t:.1f} s')
            ax.axvline(t, color=GREEN, linestyle='--', linewidth=1.5, alpha=0.9)
            ax.text(t + 0.08, args.angle_deg * 0.88,
                    f'[PASS]  t = {t:.1f} s',
                    color=GREEN, fontsize=12, va='top', fontfamily=MONO)
            readout.set_color(GREEN)
            ax.legend(loc='upper right', fontsize=11,
                      facecolor='#161b22', edgecolor=SPINE, labelcolor=FG)

        # Run until convergence + 1.5 s of steady-state, then hold
        if converged_at is not None and t > converged_at + 1.5:
            break

        # Safety exit if something goes wrong
        if t > 120.0:
            break

    plt.ioff()

    if args.save:
        fig.savefig(args.save, dpi=150, facecolor=BG)
        print(f'Plot saved → {args.save}')

    print('\nClose the plot window to exit.')
    plt.show()

    return 0 if converged_at is not None else 1


if __name__ == '__main__':
    main()
