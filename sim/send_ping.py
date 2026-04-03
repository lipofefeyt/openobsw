#!/usr/bin/env python3
"""
sim/send_ping.py — Manual test for openobsw host sim.

Spawns the sim as a subprocess, sends 10 TC(17,1) pings, and parses
the binary TM response including the sync byte.

Usage:
    python3 sim/send_ping.py
"""
import struct
import subprocess


FRAME  = bytes.fromhex("1801c0000003201101" + "00")
PACKET = struct.pack(">H", len(FRAME)) + FRAME


def parse_response(data: bytes):
    offset, count = 0, 0
    while offset < len(data):
        if data[offset] == 0xFF:
            print("  0xFF — sync byte ✓")
            offset += 1
            continue
        if offset + 2 > len(data): break
        length = struct.unpack(">H", data[offset:offset+2])[0]
        offset += 2
        if offset + length > len(data): break
        pkt = data[offset:offset+length]
        offset += length
        if len(pkt) >= 9:
            svc, subsvc = pkt[7], pkt[8]
            label = {
                (1, 1):  "TM(1,1)  acceptance ✓",
                (17, 2): "TM(17,2) pong ✓",
                (1, 7):  "TM(1,7)  completion ✓",
                (5, 1):  "TM(5,1)  event info",
            }.get((svc, subsvc), f"TM({svc},{subsvc})")
            print(f"  {label}")
            count += 1
    return count


def main():
    print("Spawning host sim, sending 10 × TC(17,1) ping...")

    proc = subprocess.Popen(
        ["./build/sim/obsw_sim"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    stdout, stderr = proc.communicate(input=PACKET * 10, timeout=5)

    print(f"\nReceived {len(stdout)} bytes:")
    count = parse_response(stdout)

    if stderr:
        print(f"\nSim log:\n{stderr.decode().strip()}")

    if count >= 3:
        print(f"\nSUCCESS — {count} TM packets + sync byte received ✓")
    else:
        print(f"\nFAILED — only {count} packets received")


if __name__ == "__main__":
    main()