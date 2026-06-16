#!/usr/bin/env python3
"""
renode/test_nominal_aocs_zynqmp.py - NOMINAL AOCS test for ZynqMP in Renode.

Verifies the full AOCS sensor/actuator loop across mode transitions:
  1. STANDBY + sensor tick  -> controller=0  (FSM gate blocks PD)
  2. STANDBY -> SAFE via TC(8,1) fid=2
  3. SAFE -> NOMINAL via TC(8,1) fid=1
  4. NOMINAL + valid ST+gyro -> controller=1, |rw_torque| > 0
  5. NOMINAL + ST invalid   -> controller=0  (sensor gate blocks PD)

Prerequisites:
    cmake -S . -B build_zynqmp_baremetal \\
        -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-none-elf.cmake \\
        -DOBSW_BUILD_ZYNQMP=ON -DOBSW_BUILD_TESTS=OFF -DOBSW_BUILD_SIM=OFF
    cmake --build build_zynqmp_baremetal -j$(nproc)
    renode renode/zynqmp_obsw.resc

Usage:
    python3 renode/test_nominal_aocs_zynqmp.py
"""
import sys
import os
import math

sys.path.insert(0, os.path.dirname(__file__))
import proto

S8_FID_RECOVER_NOMINAL = 1
S8_FID_REQUEST_SAFE    = 2

# Identity attitude - guaranteed to differ from the nadir target at t=0,
# so the PD controller sees a real error and produces non-zero RW torque.
Q_IDENTITY = (1.0, 0.0, 0.0, 0.0)   # (w, x, y, z)
OMEGA_ZERO = (0.0, 0.0, 0.0)


def check(condition, label):
    status = "PASS" if condition else "FAIL"
    print(f"  [{status}] {label}")
    return condition


def main():
    print("=== NOMINAL AOCS test (ZynqMP Renode) ===\n")
    failures = 0

    print("Connecting to Renode on localhost:3456 ...")
    try:
        sock = proto.connect()
    except OSError as e:
        print(f"ERROR: cannot connect - {e}")
        print("Is Renode running? (renode renode/zynqmp_obsw.resc)")
        sys.exit(2)
    print("Connected.")

    # Boot confirmation: ping ensures ZynqMP has reached the main loop before
    # any sensor frames are sent.  recv_tms waits up to 6 s for the pong.
    print("Waiting for ZynqMP boot (ping) ...")
    proto.send_tc(sock, proto.build_ping())
    boot_pkts = proto.recv_tms(sock, wait=6.0)
    if not proto.has_tm(boot_pkts, 17, 2):
        print(f"ERROR: no pong after 6 s (got: {proto.describe(boot_pkts)})")
        print("Is the binary loaded correctly? Try restarting Renode.")
        sys.exit(2)
    print("Boot confirmed (TM(17,2) pong received).\n")

    # Step 1: STANDBY + sensor tick -> controller=0
    print("[1] STANDBY: sensor tick must yield controller=0 (FSM gate blocks PD)")
    proto.send_sensor(sock, Q_IDENTITY, OMEGA_ZERO, t=0.0,
                      st_valid=1, gyro_valid=1)
    act, _ = proto.recv_tick(sock)
    if not check(act is not None, "actuator frame received"):
        failures += 1
    else:
        if not check(act.controller == 0,
                     f"controller=0 in STANDBY (got {act.controller})"):
            failures += 1
        rw = act.rw_torque
        if not check(all(abs(v) < 1e-9 for v in rw),
                     f"rw_torque=[0,0,0] in STANDBY (got {rw})"):
            failures += 1

    # Step 2: STANDBY -> SAFE
    print("\n[2] STANDBY -> SAFE via TC(8,1) fid=2")
    proto.send_tc(sock, proto.build_s8(S8_FID_REQUEST_SAFE))
    pkts = proto.recv_tms(sock, wait=1.0)
    print(f"    Received: {proto.describe(pkts)}")
    if not check(proto.has_tm(pkts, 1, 7), "TM(1,7) completion-ok for TC(8,1)"):
        failures += 1

    # Step 3: SAFE -> NOMINAL
    print("\n[3] SAFE -> NOMINAL via TC(8,1) fid=1")
    proto.send_tc(sock, proto.build_s8(S8_FID_RECOVER_NOMINAL))
    pkts = proto.recv_tms(sock, wait=1.0)
    print(f"    Received: {proto.describe(pkts)}")
    if not check(proto.has_tm(pkts, 1, 7), "TM(1,7) completion-ok for TC(8,1)"):
        failures += 1

    # Step 4: NOMINAL + valid ST+gyro -> controller=1, |rw_torque|>0
    print("\n[4] NOMINAL: valid ST+gyro -> controller=1, non-zero RW torque")
    proto.send_sensor(sock, Q_IDENTITY, OMEGA_ZERO, t=0.0,
                      st_valid=1, gyro_valid=1)
    act, _ = proto.recv_tick(sock)
    if not check(act is not None, "actuator frame received"):
        failures += 1
    else:
        if not check(act.controller == 1,
                     f"controller=1 in NOMINAL (got {act.controller})"):
            failures += 1
        rw_mag = math.sqrt(sum(v * v for v in act.rw_torque))
        if not check(rw_mag > 1e-9,
                     f"|rw_torque|={rw_mag:.3e} > 0 in NOMINAL"):
            failures += 1

    # Step 5: NOMINAL + ST invalid -> controller=0
    print("\n[5] NOMINAL: ST invalid -> controller=0 (sensor gate blocks PD)")
    proto.send_sensor(sock, Q_IDENTITY, OMEGA_ZERO, t=0.1,
                      st_valid=0, gyro_valid=1)
    act, _ = proto.recv_tick(sock)
    if not check(act is not None, "actuator frame received"):
        failures += 1
    else:
        if not check(act.controller == 0,
                     f"controller=0 when ST invalid (got {act.controller})"):
            failures += 1

    sock.close()

    print(f"\n{'='*50}")
    if failures == 0:
        print("RESULT: ALL CHECKS PASSED - NOMINAL AOCS loop ok")
        sys.exit(0)
    else:
        print(f"RESULT: {failures} CHECK(S) FAILED")
        sys.exit(1)


if __name__ == "__main__":
    main()
