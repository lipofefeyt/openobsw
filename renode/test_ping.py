#!/usr/bin/env python3
"""
renode/test_ping.py — Send TC(17,1) to openobsw running in Renode.

Connects to the TCP terminal exposed by Renode on port 3456.
Same protocol as the physical COM port: length-prefixed frames.

Usage:
    # Terminal 1: start Renode
    renode renode/msp430fr5969.resc

    # Terminal 2: run this script (wait for Renode to boot first)
    python3 renode/test_ping.py
"""

import socket
import struct
import time


HOST = "localhost"
PORT = 3456
TIMEOUT = 5.0


def parse_packets(raw: bytes) -> None:
    offset = 0
    count = 0
    while offset < len(raw) - 1:
        if offset + 2 > len(raw):
            break
        length = struct.unpack(">H", raw[offset:offset + 2])[0]
        if length == 0 or length > 256:
            offset += 1
            continue
        offset += 2
        if offset + length > len(raw):
            break
        pkt = raw[offset:offset + length]
        offset += length
        if len(pkt) >= 9:
            svc    = pkt[7]
            subsvc = pkt[8]
            print(f"  TM({svc},{subsvc})", end="")
            if svc == 1 and subsvc == 1:
                print(" — acceptance ✓")
            elif svc == 17 and subsvc == 2:
                print(" — are-you-alive pong ✓")
            elif svc == 1 and subsvc == 7:
                print(" — completion ✓")
            else:
                print(f" — unexpected")
            count += 1
    return count


def main():
    # TC(17,1) are-you-alive — length-prefixed space packet
    frame  = bytes.fromhex("1801c0000003201101" + "00")
    packet = struct.pack(">H", len(frame)) + frame

    print(f"Connecting to Renode UART on {HOST}:{PORT}...")
    with socket.create_connection((HOST, PORT), timeout=TIMEOUT) as sock:
        print(f"Connected. Sending TC(17,1) ping...")
        sock.sendall(packet)
        print(f"Sent: {packet.hex()}")

        time.sleep(1.0)
        sock.settimeout(TIMEOUT)
        try:
            raw = sock.recv(512)
        except socket.timeout:
            raw = b""

        print(f"Received {len(raw)} bytes: {raw.hex()}")
        if raw:
            count = parse_packets(raw)
            if count == 3:
                print("\nSUCCESS — TM(1,1) + TM(17,2) + TM(1,7) received ✓")
            else:
                print(f"\nGot {count} packets — expected 3")
        else:
            print("No response — check Renode is running and ELF loaded")


if __name__ == "__main__":
    main()