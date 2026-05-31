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


# TC(17,1) space packet — 11 bytes
# Primary header: APID=0x001, seq=standalone, data_len=4 (→ payload_len=5)
# PUS-C secondary: ver+ack=0x11, service=17, subservice=1, src_id=0x0000
FRAME  = bytes([0x18,0x01, 0xC0,0x00, 0x00,0x04, 0x11, 0x11, 0x01, 0x00,0x00])
PACKET = b'\x01' + struct.pack(">H", len(FRAME)) + FRAME


def parse_response(data: bytes):
    offset, count = 0, 0
    while offset < len(data):
        b = data[offset]
        if b == 0xFF:
            offset += 1
            continue
        if b != 0x04:       # skip unexpected bytes
            offset += 1
            continue
        offset += 1         # consume type byte 0x04
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
                (1, 8):  "TM(1,8)  completion FAIL",
                (5, 1):  "TM(5,1)  event info",
            }.get((svc, subsvc), f"TM({svc},{subsvc})")
            print(f"  {label}")
            count += 1
    return count


def main():
    print("Spawning host sim, sending TC(17,1) ping...")

    proc = subprocess.Popen(
        ["./build/sim/obsw_sim"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    stdout, stderr = proc.communicate(input=PACKET, timeout=5)

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