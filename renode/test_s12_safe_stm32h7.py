#!/usr/bin/env python3
"""
renode/test_s12_safe_stm32h7.py — S12 monitoring commands SAFE on the target.

OBSW-SRS-295: a HIGH monitoring event on the SRDB safe-trigger list commands
SAFE on the STM32H7 FreeRTOS build, as it does on the host sim.

Sequence:
    STANDBY -> SAFE -> NOMINAL                  (TC(8,1) fid 2, then fid 1)
    TC(12,3)  bdot_gain high limit 2.0e4 -> event 0x0051, HIGH
    TC(20,1)  bdot_gain = 3.0e4                 (accepted: NOMINAL)
    FDIR full tick: TM(5,4) 0x0051 with the value, FSM to SAFE
    Mode Manager tick: TM(5,4) 0x0002 safe_mode_entry
    TC(20,1)  rejected with TM(1,2)             (gated: SAFE)
    TC(8,1) fid 1 -> NOMINAL again              (a request made after the
                                                 SAFE entry is honoured)

Prerequisites:
    cmake -S targets/stm32h7 -B build_stm32h7_renode \\
        -DCMAKE_TOOLCHAIN_FILE=cmake/stm32h7-toolchain.cmake \\
        -DOBSW_ROOT=$(pwd) -DOBSW_FREERTOS=ON -DOBSW_RENODE=ON
    cmake --build build_stm32h7_renode -j$(nproc)
    renode renode/stm32h750_obsw.resc

Usage:
    python3 renode/test_s12_safe_stm32h7.py
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
import proto

S8_FID_RECOVER_NOMINAL = 1
S8_FID_REQUEST_SAFE = 2
PARAM_BDOT_GAIN = 0x20A0
EV_SAFE_MODE_ENTRY = 0x0002
EV_TEMPERATURE_SOFT_LIMIT = 0x0050
EV_TEMPERATURE_HARD_LIMIT = 0x0051   # HIGH, safe_trigger in events.yaml
SEV_HIGH = 4
LIMIT = 2.0e4
ABOVE = 3.0e4
DEFAULT_GAIN = 1.0e4


def f32_bits(value):
    return struct.unpack(">I", struct.pack(">f", value))[0]


def build_s12_add(param_id, low, high, ev_low, ev_high, severity):
    """TC(12,3): param_id | low f32 | high f32 | ev_low | ev_high | severity."""
    nan = float("nan")
    return proto.build_tc(proto.APID_DEFAULT, 12, 3,
                          struct.pack(">HffHHB", param_id,
                                      nan if low is None else low,
                                      nan if high is None else high,
                                      ev_low, ev_high, severity))


def events(packets, event_id):
    """Auxiliary data of every TM(5,4) carrying event_id (bytes after the ID)."""
    return [body[19:] for s, ss, body in packets
            if (s, ss) == (5, 4) and len(body) >= 19
            and struct.unpack(">H", body[17:19])[0] == event_id]


def check(condition, label):
    status = "PASS" if condition else "FAIL"
    print(f"  [{status}] — {label}")
    return condition


def main():
    print("=== S12 monitoring -> SAFE test (STM32H750 Renode) ===\n")
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

    # ── Reach NOMINAL: STANDBY -> SAFE -> NOMINAL ─────────────────────────
    step("[1] STANDBY -> SAFE via TC(8,1) fid=2",
         proto.build_s8(S8_FID_REQUEST_SAFE), 2.0)
    pkts = step("\n[2] SAFE -> NOMINAL via TC(8,1) fid=1",
                proto.build_s8(S8_FID_RECOVER_NOMINAL), 2.0)
    if not check(proto.has_tm(pkts, 1, 7), "TC(8,1) fid=1 completed"):
        failures += 1

    # ── Bind a HIGH safe-trigger event to a limit ─────────────────────────
    pkts = step("\n[3] TC(12,3): bdot_gain high limit 2.0e4 -> event 0x0051 HIGH",
                build_s12_add(PARAM_BDOT_GAIN, None, LIMIT,
                              EV_TEMPERATURE_SOFT_LIMIT, EV_TEMPERATURE_HARD_LIMIT,
                              SEV_HIGH), 1.5)
    if not check(proto.has_tm(pkts, 1, 7), "TC(12,3) completed (we are in NOMINAL)"):
        failures += 1
    if not check(not events(pkts, EV_TEMPERATURE_HARD_LIMIT), "no event while inside the limit"):
        failures += 1

    # ── Cross the limit ───────────────────────────────────────────────────
    # Wait covers one FDIR full tick (1 s) and the next Mode Manager tick (1 s).
    pkts = step("\n[4] TC(20,1): bdot_gain = 3.0e4, above the limit",
                proto.build_s20_set(PARAM_BDOT_GAIN, f32_bits(ABOVE)), 3.0)
    if not check(proto.has_tm(pkts, 1, 1), "TC(20,1) accepted in NOMINAL"):
        failures += 1
    hard = events(pkts, EV_TEMPERATURE_HARD_LIMIT)
    if not check(len(hard) == 1, f"one TM(5,4) 0x0051 from S12 (got {len(hard)})"):
        failures += 1
    if hard and not check(len(hard[0]) == 4 and
                          struct.unpack(">f", hard[0])[0] == ABOVE,
                          "its auxiliary data is the value as a float32"):
        failures += 1
    if not check(len(events(pkts, EV_SAFE_MODE_ENTRY)) == 1,
                 "TM(5,4) 0x0002 safe_mode_entry from the Mode Manager"):
        failures += 1

    # ── SAFE is in force ──────────────────────────────────────────────────
    pkts = step("\n[5] TC(20,1) restore gain — must be rejected in SAFE",
                proto.build_s20_set(PARAM_BDOT_GAIN, f32_bits(DEFAULT_GAIN)), 1.0)
    if not check(proto.has_tm(pkts, 1, 2) and not proto.has_tm(pkts, 1, 1),
                 "TM(1,2) rejection: the FSM gate is in SAFE"):
        failures += 1
    pkts = step("\n[6] TC(17,1) ping — still on the whitelist", proto.build_ping(), 1.0)
    if not check(proto.has_tm(pkts, 17, 2), "TM(17,2) pong in SAFE"):
        failures += 1

    # ── Ground recovery still works, and the event does not repeat ────────
    step("\n[7] SAFE -> NOMINAL via TC(8,1) fid=1 (requested after the SAFE entry)",
         proto.build_s8(S8_FID_RECOVER_NOMINAL), 2.0)
    pkts = step("\n[8] TC(20,1) restore gain — accepted again in NOMINAL",
                proto.build_s20_set(PARAM_BDOT_GAIN, f32_bits(DEFAULT_GAIN)), 2.5)
    if not check(proto.has_tm(pkts, 1, 1), "TM(1,1): recovered to NOMINAL"):
        failures += 1
    if not check(not events(pkts, EV_TEMPERATURE_HARD_LIMIT),
                 "no repeat of 0x0051 while the value stayed out of limit"):
        failures += 1

    sock.close()

    print(f"\n{'='*44}")
    if failures == 0:
        print("RESULT: ALL CHECKS PASSED — S12 -> SAFE ✓")
        sys.exit(0)
    else:
        print(f"RESULT: {failures} CHECK(S) FAILED")
        sys.exit(1)


if __name__ == "__main__":
    main()
