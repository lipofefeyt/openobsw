#!/usr/bin/env python3
"""
renode/test_ping_stm32h7.py — Send TC(17,1) to obsw_stm32h7 in Renode.

Connects to the TCP terminal exposed by Renode on port 3456.
Uses wire protocol v3 (type-prefixed frames) matching obsw_stm32h7.

Usage:
    # Terminal 1: build then start Renode
    cmake -S targets/stm32h7 -B build_stm32h7 -DCMAKE_TOOLCHAIN_FILE=cmake/stm32h7-toolchain.cmake -DOBSW_ROOT=$(pwd)
    cmake --build build_stm32h7 -j$(nproc)
    renode renode/stm32h750_obsw.resc

    # Terminal 2: run this script
    python3 renode/test_ping_stm32h7.py
"""
import socket
import struct
import time

HOST    = "localhost"
PORT    = 3456
TIMEOUT = 10.0

FRAME_TC  = 0x01
FRAME_TM  = 0x04
SYNC_BYTE = 0xFF


def build_tc_frame(apid: int, svc: int, subsvc: int,
                   data: bytes = b"") -> bytes:
    """Build a minimal PUS-C TC space packet."""
    packet_len = 6 + 5 + len(data)
    header = struct.pack(">HHH",
        0x1800 | (apid & 0x7FF),
        0xC000,
        packet_len - 7,
    )
    secondary = bytes([0x20, svc, subsvc, 0x00, 0x00])
    return header + secondary + data


def send_typed_frame(sock: socket.socket, frame_type: int,
                     data: bytes) -> None:
    sock.sendall(bytes([frame_type]) + struct.pack(">H", len(data)) + data)


def parse_response(raw: bytes) -> list:
    """Parse type-0x04 TM frames from raw bytes, skipping ASCII banner."""
    packets = []
    i = 0
    while i < len(raw) and raw[i] != FRAME_TM:
        if raw[i] == SYNC_BYTE:
            return packets
        i += 1
    while i < len(raw):
        if raw[i] == SYNC_BYTE:
            break
        if raw[i] != FRAME_TM:
            i += 1
            continue
        if i + 3 > len(raw):
            break
        length = struct.unpack(">H", raw[i+1:i+3])[0]
        i += 3
        if i + length > len(raw):
            break
        body = raw[i:i+length]
        i += length
        if len(body) >= 9:
            packets.append((body[7], body[8]))
    return packets


def main() -> None:
    tc = build_tc_frame(0x010, 17, 1)

    print(f"Connecting to Renode STM32H750 usart3 on {HOST}:{PORT}...")
    with socket.create_connection((HOST, PORT), timeout=TIMEOUT) as sock:
        print("Connected.")
        time.sleep(0.5)

        sock.settimeout(2.0)
        try:
            banner = sock.recv(256)
            print(f"Banner: {banner.decode(errors='replace').strip()}")
        except socket.timeout:
            pass

        print("Sending TC(17,1) ping...")
        send_typed_frame(sock, FRAME_TC, tc)

        time.sleep(1.0)
        sock.settimeout(TIMEOUT)
        try:
            raw = sock.recv(1024)
        except socket.timeout:
            raw = b""

        print(f"Received {len(raw)} bytes: {raw.hex()}")

        if raw:
            packets = parse_response(raw)
            print(f"Parsed {len(packets)} TM packets:")
            for svc, subsvc in packets:
                label = {
                    (1,  1):  "TM(1,1) acceptance ✓",
                    (17, 2):  "TM(17,2) pong ✓",
                    (1,  7):  "TM(1,7) completion ✓",
                }.get((svc, subsvc), f"TM({svc},{subsvc})")
                print(f"  {label}")

            if len(packets) >= 2 and any(s == 17 for s, _ in packets):
                print("\nSUCCESS — STM32H750 OBSW responding in Renode ✓")
            else:
                print("\nPartial response — expected TM(1,1) + TM(17,2) + TM(1,7)")
        else:
            print("No response — check Renode is running and ELF loaded")


if __name__ == "__main__":
    main()
