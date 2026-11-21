#!/usr/bin/env python3
"""
renode/test_hk_interval_stm32h7.py — S3 housekeeping intervals on the target.

OBSW-SRS-090, OBSW-SRS-091 on the STM32H7 FreeRTOS build (#51): TM(3,25)
arrives once per collection interval, counted in FDIR full ticks (1 s), for
each enabled set independently; TC(3,5) re-times a running set and TC(3,6)
stops it.

Intervals are measured from the on-board time in each packet's PUS-C
secondary header, and collection runs until enough reports (or enough OBT,
read from TM(17,2) pongs) have arrived, so the result does not depend on how
fast Renode runs against the wall clock (about 0.4x on the dev container).
The watchdog_kick_count field of SID 2 (one kick per full tick) is a second,
independent count of the same ticks.

Sequence:
    STANDBY -> SAFE -> NOMINAL               (TC(8,1) fid 2, then fid 1;
                                              S3 TCs are not SAFE-allowed)
    TC(3,5) SID 2 every 2 ticks, SID 7 every 3 ticks
    collect: SID 2 spacing 2 s, SID 7 spacing 3 s, kick count +2 per report
    TC(3,5) SID 2 every 5 ticks              (re-time while enabled)
    collect: SID 2 spacing 5 s
    TC(3,6) SID 2 and SID 7
    collect: no TM(3,25)
    TC(3,5) SID 99                           -> TM(1,2)

Prerequisites:
    cmake -S targets/stm32h7 -B build_stm32h7_renode \\
        -DCMAKE_TOOLCHAIN_FILE=cmake/stm32h7-toolchain.cmake \\
        -DOBSW_ROOT=$(pwd) -DOBSW_FREERTOS=ON -DOBSW_RENODE=ON
    cmake --build build_stm32h7_renode -j$(nproc)
    renode renode/stm32h750_obsw.resc

Usage:
    python3 renode/test_hk_interval_stm32h7.py
"""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import proto

S8_FID_RECOVER_NOMINAL = 1
S8_FID_REQUEST_SAFE = 2

SID_FDIR_HK = 2        # safe_mode_entry_count u16, watchdog_kick_count u32,
FDIR_HK_LEN = 10       # watchdog_ticks_remaining u32
SID_AOCS_BDOT_HK = 7   # 6 x float32, mag_valid u8
AOCS_BDOT_HK_LEN = 25
SID_UNKNOWN = 99

COLLECT_TIMEOUT = 90.0  # wall seconds per collection, generous for slow hosts

# Byte offsets in a TM packet (6 B primary header + 11 B PUS-C secondary)
OFF_MSG_COUNTER = 9    # secondary header bytes 3-4
OFF_TIME = 13          # secondary header bytes 7-10, OBT seconds, big-endian
OFF_DATA = 17          # SID, then the parameters


def build_s3_enable(sid, interval):
    return proto.build_tc(proto.APID_DEFAULT, 3, 5, struct.pack(">BI", sid, interval))


def build_s3_disable(sid):
    return proto.build_tc(proto.APID_DEFAULT, 3, 6, bytes([sid]))


def hk_reports(packets):
    """(sid, obt, msg_counter, params) of every TM(3,25), in arrival order."""
    out = []
    for s, ss, body in packets:
        if (s, ss) != (3, 25) or len(body) <= OFF_DATA:
            continue
        msg = struct.unpack(">H", body[OFF_MSG_COUNTER:OFF_MSG_COUNTER + 2])[0]
        obt = struct.unpack(">I", body[OFF_TIME:OFF_TIME + 4])[0]
        out.append((body[OFF_DATA], obt, msg, body[OFF_DATA + 1:]))
    return out


def spacings(reports, sid):
    times = [obt for s, obt, _, _ in reports if s == sid]
    return [b - a for a, b in zip(times, times[1:])]


def check(condition, label):
    status = "PASS" if condition else "FAIL"
    print(f"  [{status}] — {label}")
    return condition


def main():
    print("=== S3 housekeeping interval test (STM32H750 Renode) ===\n")
    failures = 0

    print("Connecting to Renode on localhost:3456 ...")
    try:
        sock = proto.connect()
    except OSError as e:
        print(f"ERROR: cannot connect — {e}")
        print("Is Renode running? (renode renode/stm32h750_obsw.resc)")
        sys.exit(2)
    print("Connected.\n")

    def step(label, tc, wait):
        print(label)
        proto.send_tc(sock, tc)
        pkts = proto.recv_tms(sock, wait=wait)
        print(f"    Received: {proto.describe(pkts)}")
        return pkts

    def poll():
        """Ping, then return (OBT from the pong or None, TM(3,25) received).
        OBT only appears in TM, so a ping reads the clock while no set reports."""
        proto.send_tc(sock, proto.build_ping())
        pkts = proto.recv_tms(sock, wait=0.5)
        pongs = [b for s, ss, b in pkts if (s, ss) == (17, 2)]
        obt = struct.unpack(">I", pongs[-1][OFF_TIME:OFF_TIME + 4])[0] if pongs else None
        return obt, hk_reports(pkts)

    def collect(label, want=None, obt_span=None):
        """Read TM(3,25) until `want` ({sid: count}) is met, or until `obt_span`
        seconds of on-board time have passed, or COLLECT_TIMEOUT wall seconds.
        Renode's speed relative to the wall clock varies, so no wait is fixed
        in wall time; the checks use the packets' own OBT."""
        print(label)
        start = time.monotonic()
        t0, reports = poll()
        while time.monotonic() - start < COLLECT_TIMEOUT:
            now, got = poll()
            reports += got
            if want and all(sum(1 for r in reports if r[0] == sid) >= n
                            for sid, n in want.items()):
                break
            if obt_span is not None and None not in (t0, now) and now - t0 >= obt_span:
                break
        print(f"    {len(reports)} TM(3,25) in {time.monotonic() - start:.1f} s wall: "
              + ", ".join(f"SID {s} @ {obt} s" for s, obt, _, _ in reports))
        return reports

    # ── Reach NOMINAL: STANDBY -> SAFE -> NOMINAL ─────────────────────────
    step("[1] STANDBY -> SAFE via TC(8,1) fid=2",
         proto.build_s8(S8_FID_REQUEST_SAFE), 2.0)
    pkts = step("\n[2] SAFE -> NOMINAL via TC(8,1) fid=1",
                proto.build_s8(S8_FID_RECOVER_NOMINAL), 2.0)
    if not check(proto.has_tm(pkts, 1, 7), "TC(8,1) fid=1 completed"):
        failures += 1

    # ── Two sets, two intervals ───────────────────────────────────────────
    pkts = step("\n[3] TC(3,5) SID 2 every 2 ticks", build_s3_enable(SID_FDIR_HK, 2), 0.5)
    ok = proto.has_tm(pkts, 1, 7)
    pkts = step("    TC(3,5) SID 7 every 3 ticks", build_s3_enable(SID_AOCS_BDOT_HK, 3), 0.5)
    if not check(ok and proto.has_tm(pkts, 1, 7), "both TC(3,5) completed"):
        failures += 1

    reports = collect("\n[4] Collect 5 x SID 2 and 3 x SID 7",
                      want={SID_FDIR_HK: 5, SID_AOCS_BDOT_HK: 3})
    sid2 = [r for r in reports if r[0] == SID_FDIR_HK]
    sid7 = [r for r in reports if r[0] == SID_AOCS_BDOT_HK]
    gaps2 = spacings(reports, SID_FDIR_HK)
    gaps7 = spacings(reports, SID_AOCS_BDOT_HK)
    if not check(len(sid2) >= 5 and all(g == 2 for g in gaps2),
                 f"SID 2 every 2 s of OBT (spacings {gaps2})"):
        failures += 1
    if not check(len(sid7) >= 3 and all(g == 3 for g in gaps7),
                 f"SID 7 every 3 s of OBT (spacings {gaps7})"):
        failures += 1
    if not check(all(len(p) == FDIR_HK_LEN for _, _, _, p in sid2)
                 and all(len(p) == AOCS_BDOT_HK_LEN for _, _, _, p in sid7),
                 f"report lengths match the SRDB ({FDIR_HK_LEN} B, {AOCS_BDOT_HK_LEN} B)"):
        failures += 1
    kicks = [struct.unpack(">I", p[2:6])[0] for _, _, _, p in sid2]
    kick_gaps = [b - a for a, b in zip(kicks, kicks[1:])]
    if not check(kick_gaps and all(g == 2 for g in kick_gaps),
                 f"SID 2 watchdog_kick_count +2 per report (deltas {kick_gaps})"):
        failures += 1
    msgs = [m for _, _, m, _ in reports]
    if not check(all(b == (a + 1) & 0xFFFF for a, b in zip(msgs, msgs[1:])),
                 "TM(3,25) message counter increments by one across sets"):
        failures += 1

    # ── Re-time a running set ─────────────────────────────────────────────
    pkts = step("\n[5] TC(3,5) SID 2 every 5 ticks (already enabled)",
                build_s3_enable(SID_FDIR_HK, 5), 0.5)
    if not check(proto.has_tm(pkts, 1, 7), "TC(3,5) completed"):
        failures += 1
    reports = [r for r in collect("\n[6] Collect 3 x SID 2", want={SID_FDIR_HK: 3})
               if r[0] == SID_FDIR_HK]
    gaps2 = spacings(reports, SID_FDIR_HK)
    if not check(len(reports) >= 3 and all(g == 5 for g in gaps2),
                 f"SID 2 every 5 s of OBT (spacings {gaps2})"):
        failures += 1

    # ── Stop both ─────────────────────────────────────────────────────────
    pkts = step("\n[7] TC(3,6) SID 2", build_s3_disable(SID_FDIR_HK), 0.5)
    ok = proto.has_tm(pkts, 1, 7)
    pkts = step("    TC(3,6) SID 7", build_s3_disable(SID_AOCS_BDOT_HK), 0.5)
    if not check(ok and proto.has_tm(pkts, 1, 7), "both TC(3,6) completed"):
        failures += 1
    # Anything already queued before the disable was drained by the steps above
    reports = collect("\n[8] Watch 7 s of OBT", obt_span=7)
    if not check(not reports, "no TM(3,25) in 7 s of OBT after TC(3,6)"):
        failures += 1

    # ── Unknown set ───────────────────────────────────────────────────────
    pkts = step("\n[9] TC(3,5) SID 99 (no such set)", build_s3_enable(SID_UNKNOWN, 2), 1.0)
    if not check(proto.has_tm(pkts, 1, 2) and not proto.has_tm(pkts, 1, 7),
                 "TM(1,2) rejection for an unknown SID"):
        failures += 1

    sock.close()

    print(f"\n{'='*44}")
    if failures == 0:
        print("RESULT: ALL CHECKS PASSED — S3 intervals ✓")
        sys.exit(0)
    else:
        print(f"RESULT: {failures} CHECK(S) FAILED")
        sys.exit(1)


if __name__ == "__main__":
    main()
