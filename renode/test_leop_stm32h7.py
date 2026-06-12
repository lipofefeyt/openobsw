#!/usr/bin/env python3
"""
renode/test_leop_stm32h7.py — LEOP end-to-end mode sequence (#64).

Exercises the full STANDBY → SAFE → NOMINAL lifecycle via ground commands:
  1. Boot: STANDBY — ping must work (S17 on whitelist)
  2. TC(8,1) fid=2: STANDBY → SAFE
  3. SAFE: ping still works
  4. TC(8,1) fid=1: SAFE → NOMINAL
  5. NOMINAL: ping works, all TCs accepted

Prerequisites:
    cmake -S targets/stm32h7 -B build_stm32h7_renode \\
        -DCMAKE_TOOLCHAIN_FILE=cmake/stm32h7-toolchain.cmake \\
        -DOBSW_ROOT=$(pwd) -DOBSW_FREERTOS=ON -DOBSW_RENODE=ON
    cmake --build build_stm32h7_renode -j$(nproc)
    renode renode/stm32h750_obsw.resc

Usage:
    python3 renode/test_leop_stm32h7.py
"""
import sys
import os

sys.path.insert(0, os.path.dirname(__file__))
import proto

S8_FID_REQUEST_SAFE    = 2
S8_FID_RECOVER_NOMINAL = 1


def check(condition, label):
    status = "PASS" if condition else "FAIL"
    print(f"  [{status}] {condition and 'ok' or 'MISSING'} — {label}")
    return condition


def main():
    print("=== LEOP e2e test (STM32H750 Renode) ===\n")
    failures = 0

    print("Connecting to Renode on localhost:3456 ...")
    try:
        sock = proto.connect()
    except OSError as e:
        print(f"ERROR: cannot connect — {e}")
        print("Is Renode running? (renode renode/stm32h750_obsw.resc)")
        sys.exit(2)
    print("Connected. Waiting for FreeRTOS scheduler to boot ...")

    # ── Step 1: STANDBY — ping must work (S17 on whitelist) ──────────────
    print("\n[1] STANDBY: ping")
    proto.send_tc(sock, proto.build_ping())
    pkts = proto.recv_tms(sock, wait=1.5)
    print(f"    Received: {proto.describe(pkts)}")
    if not check(proto.has_tm(pkts, 17, 2), "TM(17,2) pong received in STANDBY"):
        failures += 1

    # ── Step 2: STANDBY → SAFE via TC(8,1) fid=2 ─────────────────────────
    print("\n[2] STANDBY → SAFE via TC(8,1) fid=2")
    proto.send_tc(sock, proto.build_s8(S8_FID_REQUEST_SAFE))
    pkts = proto.recv_tms(sock, wait=2.0)   # 1 s for Mode Manager tick + buffer
    print(f"    Received: {proto.describe(pkts)}")
    if not check(proto.has_tm(pkts, 1, 1), "TM(1,1) acceptance-ok for TC(8,1)"):
        failures += 1
    if not check(proto.has_tm(pkts, 1, 7), "TM(1,7) completion-ok for TC(8,1)"):
        failures += 1

    # ── Step 3: SAFE — ping must still work ───────────────────────────────
    print("\n[3] SAFE: ping")
    proto.send_tc(sock, proto.build_ping())
    pkts = proto.recv_tms(sock, wait=1.0)
    print(f"    Received: {proto.describe(pkts)}")
    if not check(proto.has_tm(pkts, 17, 2), "TM(17,2) pong received in SAFE"):
        failures += 1

    # ── Step 4: SAFE → NOMINAL via TC(8,1) fid=1 ─────────────────────────
    print("\n[4] SAFE → NOMINAL via TC(8,1) fid=1")
    proto.send_tc(sock, proto.build_s8(S8_FID_RECOVER_NOMINAL))
    pkts = proto.recv_tms(sock, wait=2.0)
    print(f"    Received: {proto.describe(pkts)}")
    if not check(proto.has_tm(pkts, 1, 1), "TM(1,1) acceptance-ok for TC(8,1)"):
        failures += 1
    if not check(proto.has_tm(pkts, 1, 7), "TM(1,7) completion-ok for TC(8,1)"):
        failures += 1

    # ── Step 5: NOMINAL — ping and unrestricted TCs work ─────────────────
    print("\n[5] NOMINAL: ping")
    proto.send_tc(sock, proto.build_ping())
    pkts = proto.recv_tms(sock, wait=1.0)
    print(f"    Received: {proto.describe(pkts)}")
    if not check(proto.has_tm(pkts, 17, 2), "TM(17,2) pong received in NOMINAL"):
        failures += 1

    # In NOMINAL, TC(20,1) passes the FSM gate → TM(1,1) acceptance
    print("\n[5b] NOMINAL: TC(20,1) set param accepted (FSM gate open)")
    # bdot_gain (0x20A0) = 1e4 → 0x4620C49B in IEEE-754 BE
    proto.send_tc(sock, proto.build_s20_set(0x20A0, 0x4620C49B))
    pkts = proto.recv_tms(sock, wait=1.0)
    print(f"    Received: {proto.describe(pkts)}")
    if not check(proto.has_tm(pkts, 1, 1), "TM(1,1) acceptance-ok (not TM(1,2)) in NOMINAL"):
        failures += 1

    sock.close()

    print(f"\n{'='*44}")
    if failures == 0:
        print("RESULT: ALL CHECKS PASSED — LEOP sequence ✓")
        sys.exit(0)
    else:
        print(f"RESULT: {failures} CHECK(S) FAILED")
        sys.exit(1)


if __name__ == "__main__":
    main()
