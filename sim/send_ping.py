#!/usr/bin/env python3
"""
send_ping.py — manually inject a TC(17,1) are-you-alive into obsw_sim.

Usage:
    python3 sim/send_ping.py | ./build/sim/obsw_sim

Expected output (stderr):
    [OBSW] Host sim started. Awaiting TC frames on stdin.
    [OBSW] S17/1 are-you-alive received on APID 0x010

Expected output (stdout):
    ACK apid=0x010 svc=17 subsvc=1 flags=0x09 seq=1
"""

import sys
import struct

# ---------------------------------------------------------------------------
# Build a PUS-C TC space packet: S17/1 are-you-alive on APID 0x010
# ---------------------------------------------------------------------------

APID       = 0x010
SEQ_FLAGS  = 0b11          # unsegmented
SEQ_COUNT  = 1
DATA_LEN   = 3             # 4 bytes payload - 1

# Primary header (6 bytes)
word0 = (0 << 13) | (1 << 12) | (1 << 11) | APID   # TC, sec_hdr=1
word1 = (SEQ_FLAGS << 14) | SEQ_COUNT
word2 = DATA_LEN

primary_hdr = struct.pack('>HHH', word0, word1, word2)

# PUS-C TC secondary header (4 bytes): PUS ver+ack, svc, subsvc, src_id_hi
secondary_hdr = bytes([0x11, 17, 1, 0x00])

frame = primary_hdr + secondary_hdr

# Length-prefix (2-byte big-endian) + frame
sys.stdout.buffer.write(struct.pack('>H', len(frame)))
sys.stdout.buffer.write(frame)
sys.stdout.buffer.flush()