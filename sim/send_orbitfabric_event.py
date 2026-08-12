#!/usr/bin/env python3
"""
sim/send_orbitfabric_event.py — Manual OrbitFabric event materialization test.

Spawns the OrbitFabric-enabled host sim, sends TC(8,1) with the host-sim
OrbitFabric voltage-out-of-bounds function ID, and verifies that OpenOBSW
emits TM(5,3) carrying event_id 0x5001 in the packet application data.

Usage:
    python3 sim/send_orbitfabric_event.py
"""
import struct
import subprocess
import sys


OF_EVENT_VOLTAGE_OUT_OF_BOUNDS = 0x5001
OBSW_OF_S8_FN_REPORT_VOLTAGE_OUT_OF_BOUNDS = 0x5001

# TC(8,1) Perform Function.
#
# Space packet:
#   primary header + PUS-C TC secondary header + S8 user data
#
# S8 user data:
#   function_id (2 bytes BE) | args_len (1 byte)
TC = bytes([
    0x18, 0x01,  # primary header: version/type/sec-hdr/APID
    0xC0, 0x00,  # sequence flags/count
    0x00, 0x07,  # data length = 8 bytes - 1
    0x11,        # PUS version / ack flags
    0x08,        # service = 8
    0x01,        # subservice = 1
    0x00, 0x00,  # source id
    (OBSW_OF_S8_FN_REPORT_VOLTAGE_OUT_OF_BOUNDS >> 8) & 0xFF,
    OBSW_OF_S8_FN_REPORT_VOLTAGE_OUT_OF_BOUNDS & 0xFF,
    0x00,        # args_len = 0
])

FRAME = b"\x01" + struct.pack(">H", len(TC)) + TC


def parse_tm_frames(data: bytes) -> list[bytes]:
    packets: list[bytes] = []
    offset = 0

    while offset < len(data):
        frame_type = data[offset]
        if frame_type == 0xFF:
            offset += 1
            continue

        if frame_type != 0x04:
            offset += 1
            continue

        offset += 1
        if offset + 2 > len(data):
            break

        length = struct.unpack(">H", data[offset:offset + 2])[0]
        offset += 2

        if offset + length > len(data):
            break

        packets.append(data[offset:offset + length])
        offset += length

    return packets


def main() -> int:
    proc = subprocess.Popen(
        ["./build_stage_of_event_orbitfabric/sim/obsw_sim"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    stdout, stderr = proc.communicate(input=FRAME, timeout=5)

    if stderr:
        print(stderr.decode(errors="replace").strip())

    packets = parse_tm_frames(stdout)

    for pkt in packets:
        if len(pkt) < 9:
            continue
        svc = pkt[7]
        subsvc = pkt[8]
        print(f"TM({svc},{subsvc}) len={len(pkt)} raw={pkt.hex()}")

    tm53 = [p for p in packets if len(p) >= 19 and p[7] == 5 and p[8] == 3]
    if not tm53:
        print("FAILED — no TM(5,3) packet observed")
        return 1

    pkt = tm53[0]
    event_id = (pkt[17] << 8) | pkt[18]

    if event_id != OF_EVENT_VOLTAGE_OUT_OF_BOUNDS:
        print(
            "FAILED — TM(5,3) event_id mismatch: "
            f"expected 0x{OF_EVENT_VOLTAGE_OUT_OF_BOUNDS:04X}, "
            f"got 0x{event_id:04X}"
        )
        return 1

    print(
        "SUCCESS — TC(8,1) triggered TM(5,3) with "
        f"event_id 0x{event_id:04X} at raw[17:19]"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
