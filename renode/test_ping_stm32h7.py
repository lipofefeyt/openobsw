#!/usr/bin/env python3
"""
renode/test_ping_stm32h7.py — Send TC(17,1) to obsw_stm32h7 in Renode.

Connects to the TCP terminal exposed by Renode on port 3456 and expects
TM(1,1), TM(17,2) and TM(1,7) back. Works against both the superloop and the
FreeRTOS build. Framing and parsing come from renode/proto.py (wire protocol
v4), which resyncs on the 0x04 TM type byte, so an end-of-tick 0xFF left
over from an earlier tick does not hide the reply (#114).

Exit status: 0 if all three reports arrive, 1 otherwise.

Usage:
    # Terminal 1: build then start Renode
    cmake -S targets/stm32h7 -B build_stm32h7_renode \\
        -DCMAKE_TOOLCHAIN_FILE=cmake/stm32h7-toolchain.cmake \\
        -DOBSW_ROOT=$(pwd) -DOBSW_RENODE=ON [-DOBSW_FREERTOS=ON]
    cmake --build build_stm32h7_renode -j$(nproc)
    renode renode/stm32h750_obsw.resc

    # Terminal 2: run this script
    python3 renode/test_ping_stm32h7.py
"""
import os
import socket
import sys

sys.path.insert(0, os.path.dirname(__file__))
import proto

EXPECTED = ((1, 1), (17, 2), (1, 7))


def main() -> int:
    print(f"Connecting to Renode STM32H750 usart3 on {proto.HOST}:{proto.PORT}...")
    try:
        sock = socket.create_connection((proto.HOST, proto.PORT), timeout=proto.TIMEOUT)
    except OSError as exc:
        print(f"ERROR: cannot connect — {exc}")
        print("Is Renode running? (renode renode/stm32h750_obsw.resc)")
        return 1

    with sock:
        print("Connected.")
        sock.settimeout(2.0)
        try:
            banner = sock.recv(256)
            print(f"Banner: {banner.decode(errors='replace').strip()}")
        except socket.timeout:
            pass
        sock.settimeout(proto.TIMEOUT)

        print("Sending TC(17,1) ping...")
        proto.send_tc(sock, proto.build_ping())
        packets = proto.recv_tms(sock, wait=1.0)

    print(f"Parsed {len(packets)} TM packets: {proto.describe(packets)}")
    missing = [f"TM({s},{ss})" for s, ss in EXPECTED if not proto.has_tm(packets, s, ss)]
    if missing:
        print(f"\nFAIL — missing {', '.join(missing)}")
        return 1
    print("\nSUCCESS — STM32H750 OBSW responding in Renode ✓")
    return 0


if __name__ == "__main__":
    sys.exit(main())
