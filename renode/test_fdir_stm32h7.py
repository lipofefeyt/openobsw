#!/usr/bin/env python3
"""
renode/test_fdir_stm32h7.py — FDIR whitelist enforcement test (#65).

Verifies that the TC whitelist blocks non-whitelisted commands in SAFE mode
while whitelisted commands (S17 ping, S8 mode transitions) continue to work.

Whitelist (enforced in STANDBY and SAFE):
    TC(8,1)  — mode transitions
    TC(17,1) — ping
    TC(20,3) — get parameter

Non-whitelisted (must be rejected with TM(1,2) in SAFE):
    TC(20,1) — set parameter
    TC(3,5)  — enable HK set

Prerequisites:
    cmake -S targets/stm32h7 -B build_stm32h7_renode \\
        -DCMAKE_TOOLCHAIN_FILE=cmake/stm32h7-toolchain.cmake \\
        -DOBSW_ROOT=$(pwd) -DOBSW_FREERTOS=ON -DOBSW_RENODE=ON
    cmake --build build_stm32h7_renode -j$(nproc)
    renode renode/stm32h750_obsw.resc

Usage:
    python3 renode/test_fdir_stm32h7.py
"""
import sys
import os

sys.path.insert(0, os.path.dirname(__file__))
import proto

S8_FID_REQUEST_SAFE = 2


def check(condition, label):
    status = "PASS" if condition else "FAIL"
    print(f"  [{status}] — {label}")
    return condition


def main():
    print("=== FDIR whitelist enforcement test (STM32H750 Renode) ===\n")
    failures = 0

    print("Connecting to Renode on localhost:3456 ...")
    try:
        sock = proto.connect()
    except OSError as e:
        print(f"ERROR: cannot connect — {e}")
        print("Is Renode running? (renode renode/stm32h750_obsw.resc)")
        sys.exit(2)
    print("Connected.\n")

    # ── Step 1: Enter SAFE mode ───────────────────────────────────────────
    print("[1] Enter SAFE via TC(8,1) fid=2")
    proto.send_tc(sock, proto.build_s8(S8_FID_REQUEST_SAFE))
    pkts = proto.recv_tms(sock, wait=2.0)
    print(f"    Received: {proto.describe(pkts)}")
    if not check(proto.has_tm(pkts, 1, 7), "TC(8,1) completed — now in SAFE"):
        failures += 1

    # ── Step 2: Non-whitelisted TC(20,1) must be rejected ────────────────
    print("\n[2] TC(20,1) set param — must be rejected in SAFE")
    proto.send_tc(sock, proto.build_s20_set(0x20A0, 0x4620C49B))
    pkts = proto.recv_tms(sock, wait=1.0)
    print(f"    Received: {proto.describe(pkts)}")
    if not check(proto.has_tm(pkts, 1, 2), "TM(1,2) rejection for TC(20,1) in SAFE"):
        failures += 1
    if not check(not proto.has_tm(pkts, 1, 1), "no TM(1,1) acceptance in SAFE"):
        failures += 1

    # ── Step 3: Whitelisted TC(17,1) must still work ─────────────────────
    print("\n[3] TC(17,1) ping — must work in SAFE (on whitelist)")
    proto.send_tc(sock, proto.build_ping())
    pkts = proto.recv_tms(sock, wait=1.0)
    print(f"    Received: {proto.describe(pkts)}")
    if not check(proto.has_tm(pkts, 17, 2), "TM(17,2) pong received in SAFE"):
        failures += 1

    # ── Step 4: TC(3,5) enable HK — also not on whitelist ────────────────
    print("\n[4] TC(3,5) enable HK set — must be rejected in SAFE")
    hk_tc = proto.build_tc(proto.APID_DEFAULT, 3, 5, b"\x01")  # set_id=1
    proto.send_tc(sock, hk_tc)
    pkts = proto.recv_tms(sock, wait=1.0)
    print(f"    Received: {proto.describe(pkts)}")
    if not check(proto.has_tm(pkts, 1, 2), "TM(1,2) rejection for TC(3,5) in SAFE"):
        failures += 1

    # ── Step 5: Whitelisted TC(20,3) get param must work ─────────────────
    print("\n[5] TC(20,3) get param — must work in SAFE (on whitelist)")
    get_tc = proto.build_tc(proto.APID_DEFAULT, 20, 3,
                             b"\x20\xA0")  # param_id=bdot_gain
    proto.send_tc(sock, get_tc)
    pkts = proto.recv_tms(sock, wait=1.0)
    print(f"    Received: {proto.describe(pkts)}")
    if not check(proto.has_tm(pkts, 20, 2), "TM(20,2) param report received in SAFE"):
        failures += 1

    sock.close()

    print(f"\n{'='*44}")
    if failures == 0:
        print("RESULT: ALL CHECKS PASSED — FDIR whitelist ✓")
        sys.exit(0)
    else:
        print(f"RESULT: {failures} CHECK(S) FAILED")
        sys.exit(1)


if __name__ == "__main__":
    main()
