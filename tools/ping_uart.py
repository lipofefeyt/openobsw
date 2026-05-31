#!/usr/bin/env python3
"""
tools/ping_uart.py — Send TC(17,1) ping to STM32H750 over UART (wire protocol v3).

Usage:
    python3 tools/ping_uart.py [/dev/ttyUSB0] [--count N]

Requires: pip install pyserial

Wire protocol v3 (uplink / downlink):
    → 0x01 [uint16 BE len] [space packet bytes]   TC uplink
    ← 0x04 [uint16 BE len] [TM packet bytes]      TM downlink
    ← 0xFF                                         end-of-tick
"""

import argparse
import struct
import sys
import time

# ---------------------------------------------------------------------------
# TC(17,1) space packet (11 bytes)
#
# Primary header (6 bytes):
#   1801  — packet version=0, type=TC(1), sec_hdr=1, APID=0x001
#   C000  — seq flags=standalone(11), seq count=0
#   0004  — data_len=4  →  payload_len = data_len+1 = 5 bytes
# PUS-C secondary header (5 bytes):
#   11    — PUS ver + ack flags
#   11    — service = 17
#   01    — subservice = 1
#   0000  — source ID
# ---------------------------------------------------------------------------
TC_PING = bytes([
    0x18, 0x01,        # packet ID
    0xC0, 0x00,        # sequence control
    0x00, 0x04,        # data length = 4
    0x11,              # PUS-C secondary hdr: ver + ack
    0x11,              # service = 17
    0x01,              # subservice = 1
    0x00, 0x00,        # source ID
])


def frame_tc(packet: bytes) -> bytes:
    """Wrap a space packet in wire protocol v3 TC uplink frame."""
    return b'\x01' + struct.pack('>H', len(packet)) + packet


TM_LABELS = {
    (1,  1): 'TM(1,1)  acceptance ✓',
    (1,  2): 'TM(1,2)  acceptance FAIL',
    (1,  7): 'TM(1,7)  completion ✓',
    (1,  8): 'TM(1,8)  completion FAIL',
    (17, 2): 'TM(17,2) pong ✓',
    (5,  1): 'TM(5,1)  event INFO',
    (5,  2): 'TM(5,2)  event LOW',
    (5,  3): 'TM(5,3)  event MEDIUM',
    (5,  4): 'TM(5,4)  event HIGH',
}


def decode_tm_packets(data: bytes) -> list[tuple[int, int, bytes]]:
    """Parse wire protocol v3 TM stream; return list of (service, subservice, payload)."""
    results = []
    i = 0
    while i < len(data):
        b = data[i]
        if b == 0xFF:       # end-of-tick
            i += 1
            continue
        if b != 0x04:       # unexpected byte
            i += 1
            continue
        if i + 3 > len(data):
            break
        length = struct.unpack('>H', data[i+1:i+3])[0]
        i += 3
        if i + length > len(data):
            break
        pkt = data[i:i+length]
        i += length
        # PUS-C TM space packet: 6 primary + secondary header
        # service at pkt[7], subservice at pkt[8]  (offset within space packet)
        if len(pkt) >= 9:
            results.append((pkt[7], pkt[8], pkt[9:]))
    return results


def run(port: str, count: int, timeout: float) -> bool:
    try:
        import serial
    except ImportError:
        print('ERROR: pyserial not installed.  Run: pip install pyserial', file=sys.stderr)
        sys.exit(1)

    uplink = frame_tc(TC_PING)
    all_ok = True

    with serial.Serial(port, baudrate=115200, timeout=timeout) as ser:
        for n in range(1, count + 1):
            print(f'\n--- ping {n}/{count} ---')
            ser.reset_input_buffer()
            ser.write(uplink)
            ser.flush()

            # Read until we see 0xFF (end-of-tick) or timeout
            raw = b''
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                chunk = ser.read(ser.in_waiting or 1)
                raw += chunk
                if b'\xff' in raw:
                    break

            packets = decode_tm_packets(raw)
            if not packets:
                print('  (no TM received)')
                all_ok = False
            else:
                for svc, subsvc, _ in packets:
                    label = TM_LABELS.get((svc, subsvc), f'TM({svc},{subsvc})')
                    print(f'  {label}')

            got_pong = any(svc == 17 and subsvc == 2 for svc, subsvc, _ in packets)
            if not got_pong:
                print('  WARN: no pong received')
                all_ok = False

            if n < count:
                time.sleep(0.1)

    return all_ok


def main():
    parser = argparse.ArgumentParser(description='Send TC(17,1) ping to STM32H750 over UART')
    parser.add_argument('port', nargs='?', default='/dev/ttyUSB0', help='Serial port (default: /dev/ttyUSB0)')
    parser.add_argument('--count', type=int, default=3, help='Number of pings (default: 3)')
    parser.add_argument('--timeout', type=float, default=2.0, help='Per-ping read timeout in seconds (default: 2.0)')
    args = parser.parse_args()

    print(f'Sending {args.count} TC(17,1) ping(s) to {args.port} at 115200 baud')
    ok = run(args.port, args.count, args.timeout)
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
