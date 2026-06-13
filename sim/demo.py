#!/usr/bin/env python3
"""
sim/demo.py — ADCS nadir-pointing convergence demo (headless MP4 output).

Runs the openobsw sim, collects the convergence data, then renders a
720p MP4 animation — no display or screen recording required.

The output file can be uploaded directly to LinkedIn as a video post.

Usage
-----
  source .venv/bin/activate
  python3 sim/demo.py [--output demo.mp4]

Requirements: matplotlib, imageio[ffmpeg]  (both in requirements)
"""

import math
import os
import struct
import subprocess
import sys

import matplotlib
matplotlib.use('Agg')           # headless — no display needed
import matplotlib.pyplot as plt
import numpy as np
import imageio

_SIM_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _SIM_DIR)

from adcs_harness import (      # noqa: E402
    nadir_quat, attitude_error_rad, quat_from_axis_angle,
    quat_multiply, PASS_THRESHOLD_DEG, PASS_THRESHOLD_RAD,
    R_EARTH,
)
from wire_proto import (        # noqa: E402
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
BG      = '#0d1117'
FG      = '#c9d1d9'
TITLE   = '#e6edf3'
BLUE    = '#58a6ff'
RED     = '#f85149'
GREEN   = '#3fb950'
GRID    = '#21262d'
SPINE   = '#30363d'
MONO    = 'monospace'


# ── CLI ───────────────────────────────────────────────────────────────────────

def _parse():
    import argparse
    repo = os.path.dirname(_SIM_DIR)
    p = argparse.ArgumentParser(
        description='Render ADCS convergence demo to MP4 (no display required)',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument('--sim',       default=os.path.join(repo, 'build', 'sim', 'obsw_sim'),
                   metavar='PATH', help='obsw_sim binary')
    p.add_argument('--dt',        type=float, default=0.01,  metavar='S',
                   help='integration time step')
    p.add_argument('--angle-deg', type=float, default=45.0,
                   help='initial attitude error from nadir [deg]')
    p.add_argument('--fps',       type=int,   default=25,
                   help='output video frame rate')
    p.add_argument('--output',    default='demo.mp4', metavar='PATH',
                   help='output MP4 path')
    return p.parse_args()


# ── Simulation ────────────────────────────────────────────────────────────────

def _simulate(args, proc):
    """Run the sim and return (history_t, history_err, converged_at, meta)."""
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

    meta = dict(alt_km=alt_km, inc_deg=inc_deg, kp=kp, kd=kd)

    print(f'  Orbit  : {alt_km:.0f} km  ·  {inc_deg:.1f}° inclination')
    print(f'  ADCS   : Kp={kp:.2f}  Kd={kd:.2f}')
    print(f'  Error₀ : {args.angle_deg:.0f}° from nadir')

    q_nadir_0 = nadir_quat(0.0, r_orbit, inc_rad)
    q_err_0   = quat_from_axis_angle([0, 0, 1], math.radians(args.angle_deg))
    q         = quat_multiply(q_nadir_0, q_err_0)
    omega     = np.zeros(3)

    print('\nCommanding NOMINAL mode via TC(8,1)...')
    send_tc(proc, build_tc(APID_DEFAULT, 8, 1, struct.pack('>HB', 1, 0)))
    drain_tc_response(proc)

    history_t   = []
    history_err = []
    converged_at = None
    t = 0.0

    print('Simulating...')
    while True:
        send_sensor(proc, NO_FIELD, q, omega, t,
                    mag_valid=0, st_valid=1, gyro_valid=1)
        act = recv_tick(proc)

        omega = omega_step(omega, act.rw_torque, I, I_inv, args.dt)
        q     = quat_step(q, omega, args.dt)
        t    += args.dt

        q_tgt   = nadir_quat(t, r_orbit, inc_rad)
        err_rad = attitude_error_rad(q, q_tgt)
        err_deg = math.degrees(err_rad)

        history_t.append(t)
        history_err.append(err_deg)

        if converged_at is None and err_rad < PASS_THRESHOLD_RAD:
            converged_at = t
            print(f'  [PASS] nadir error = {err_deg:.2f}°  at t = {t:.1f} s')

        # Collect 0.8 s of steady-state after convergence, then stop
        if converged_at is not None and t > converged_at + 0.8:
            break
        if t > 120.0:
            break

    return history_t, history_err, converged_at, meta


# ── Render ────────────────────────────────────────────────────────────────────

def _render(args, history_t, history_err, converged_at, meta):
    """Render frames to MP4 via imageio."""

    # Figure: 1280×720 (16:9 720p)
    fig, ax = plt.subplots(figsize=(12.8, 7.2), dpi=100)
    fig.patch.set_facecolor(BG)
    ax.set_facecolor(BG)

    # Static elements
    ax.axhline(PASS_THRESHOLD_DEG, color=RED, linestyle='--', linewidth=1.8,
               label=f'{PASS_THRESHOLD_DEG}° pass threshold', zorder=2)
    ax.set_xlim(0, history_t[-1])
    ax.set_ylim(0, args.angle_deg * 1.12)
    ax.set_xlabel('Simulation time  [s]',       color=FG,    fontsize=14)
    ax.set_ylabel('Nadir pointing error  [°]',  color=FG,    fontsize=14)
    ax.set_title(
        'openobsw  —  ADCS Nadir-Pointing Convergence\n'
        f'PD quaternion controller  ·  {meta["alt_km"]:.0f} km SSO  '
        f'·  Kp={meta["kp"]:.2f}  Kd={meta["kd"]:.2f}',
        color=TITLE, fontsize=14, pad=14,
    )
    ax.tick_params(colors='#8b949e', labelsize=12)
    for sp in ax.spines.values():
        sp.set_edgecolor(SPINE)
    ax.grid(True, color=GRID, linewidth=0.9)

    # Dynamic elements
    line, = ax.plot([], [], color=BLUE, linewidth=2.4,
                    label='Nadir pointing error', zorder=3)
    readout = ax.text(0.02, 0.94, '', transform=ax.transAxes,
                      color=BLUE, fontsize=15, va='top', fontfamily=MONO)
    ax.legend(loc='upper right', fontsize=12,
              facecolor='#161b22', edgecolor=SPINE, labelcolor=FG)
    fig.tight_layout()

    converged_frame = None
    if converged_at is not None:
        # find the first frame index where t >= converged_at
        converged_frame = next(
            i for i, t in enumerate(history_t) if t >= converged_at
        )

    n_frames = len(history_t)
    print(f'\nRendering {n_frames} frames at {args.fps} fps → {args.output}')

    with imageio.get_writer(args.output, fps=args.fps,
                            codec='libx264', quality=8,
                            pixelformat='yuv420p') as writer:
        pass_marker_added = False
        for i in range(n_frames):
            line.set_data(history_t[:i + 1], history_err[:i + 1])
            readout.set_text(f'error = {history_err[i]:5.2f}°')

            if converged_frame is not None and i >= converged_frame \
                    and not pass_marker_added:
                ax.axvline(history_t[converged_frame], color=GREEN,
                           linestyle='--', linewidth=1.8, alpha=0.9, zorder=2)
                ax.text(history_t[converged_frame] + history_t[-1] * 0.015,
                        args.angle_deg * 0.85,
                        f'[PASS]  t = {converged_at:.1f} s',
                        color=GREEN, fontsize=13, va='top', fontfamily=MONO)
                readout.set_color(GREEN)
                pass_marker_added = True

            fig.canvas.draw()
            frame = np.asarray(fig.canvas.buffer_rgba())[:, :, :3]
            writer.append_data(frame)

            if (i + 1) % 50 == 0 or i == n_frames - 1:
                print(f'  {i + 1}/{n_frames} frames', end='\r')

    plt.close(fig)
    print(f'\nSaved → {args.output}  '
          f'({n_frames / args.fps:.1f} s  ·  {os.path.getsize(args.output) // 1024} KB)')


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    args = _parse()

    if not os.path.exists(args.sim):
        print(f'ERROR: sim binary not found: {args.sim}')
        print('  Build first:  cmake --build build -j$(nproc)')
        sys.exit(2)

    print('openobsw  ·  ADCS convergence demo')
    print('─' * 42)

    proc = subprocess.Popen(
        [args.sim],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
    )
    try:
        history_t, history_err, converged_at, meta = _simulate(args, proc)
    except (EOFError, BrokenPipeError) as exc:
        print(f'\nERROR: sim process died: {exc}')
        sys.exit(1)
    finally:
        try:
            proc.stdin.close()
        except OSError:
            pass
        proc.wait()

    _render(args, history_t, history_err, converged_at, meta)
    print('\nDone. Upload demo.mp4 directly to LinkedIn.')


if __name__ == '__main__':
    main()
