#!/usr/bin/env python3
"""
renode/test_ping_zynqmp.py — Send TC(17,1) to obsw_zynqmp in Renode.

Connects to the TCP terminal exposed by Renode on port 3456.
Uses wire protocol v3 (type-prefixed frames) matching obsw_zynqmp.

Usage:
    # Terminal 1: start Renode
    renode renode/zynqmp_obsw.resc
    # Terminal 2: run this script
    python3 renode/test_ping_zynqmp.py
"""
import socket
import struct
import time

HOST    = "localhost"
PORT    = 3456
TIMEOUT = 10.0

FRAME_TC       = 0x01
FRAME_SENSOR   = 0x02
FRAME_ACTUATOR = 0x03
FRAME_TM       = 0x04
SYNC_BYTE      = 0xFF


def build_tc_frame(apid: int, svc: int, subsvc: int,
                   data: bytes = b"") -> bytes:
    """Build a minimal PUS-C TC space packet."""
    data_len = len(data)
    packet_len = 6 + 3 + data_len  # primary + secondary + data
    header = struct.pack(">HHH",
        0x1800 | (apid & 0x7FF),    # version=0, type=1(TC), APID
        0xC000,                      # seq flags=11 (standalone), count=0
        packet_len - 7,              # data field length - 1
    )
    secondary = bytes([0x20, svc, subsvc, 0x00, 0x00])  # PUS-C: ver+ack, svc, subsvc, src_id(2)
    return header + secondary + data


def send_typed_frame(sock: socket.socket, frame_type: int,
                     data: bytes) -> None:
    length = len(data)
    header = bytes([frame_type]) + struct.pack(">H", length)
    sock.sendall(header + data)


def parse_response(raw: bytes) -> list:
    """Parse type-prefixed frames from raw bytes, skipping debug text."""
    packets = []
    i = 0
    # Skip debug ASCII text — find first 0x04 TM frame marker
    while i < len(raw) and raw[i] != FRAME_TM:
        if raw[i] == SYNC_BYTE:
            break
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
            svc    = body[7]
            subsvc = body[8]
            packets.append((svc, subsvc))
    return packets


def main() -> None:
    tc = bytes.fromhex('1810c0000003201101' + '00')  # PUS-C TC(17,1) APID=0x010

    print(f"Connecting to Renode ZynqMP uart0 on {HOST}:{PORT}...")
    with socket.create_connection((HOST, PORT), timeout=TIMEOUT) as sock:
        print("Connected.")
        time.sleep(0.5)  # wait for OBSW startup banner

        # Read startup banner
        sock.settimeout(2.0)
        try:
            banner = sock.recv(256)
            print(f"Banner: {banner.decode(errors='replace').strip()}")
        except socket.timeout:
            pass

        # Send TC(17,1) as type-0x01 frame
        print("Sending TC(17,1) ping...")
        send_typed_frame(sock, FRAME_TC, tc)

        # Wait for response
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
                    (1, 1): "TM(1,1) acceptance ✓",
                    (17, 2): "TM(17,2) pong ✓",
                    (1, 7): "TM(1,7) completion ✓",
                }.get((svc, subsvc), f"TM({svc},{subsvc})")
                print(f"  {label}")

            if len(packets) >= 2 and any(s == 17 for s, _ in packets):
                print("\nSUCCESS — ZynqMP OBSW responding in Renode ✓")
            else:
                print(f"\nPartial response — expected TM(1,1) + TM(17,2) + TM(1,7)")
        else:
            print("No response — check Renode is running and ELF loaded")


if __name__ == "__main__":
    main()
