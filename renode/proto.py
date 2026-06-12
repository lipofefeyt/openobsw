"""renode/proto.py — Wire protocol v3 helpers for openobsw Renode tests.

Usage:
    import proto
    sock = proto.connect()
    proto.send_tc(sock, proto.build_ping())
    pkts = proto.recv_tms(sock, wait=1.5)
    assert proto.has_tm(pkts, 17, 2), "no pong"
"""
import socket
import struct
import time

HOST = "localhost"
PORT = 3456
TIMEOUT = 10.0

FRAME_TC  = 0x01
FRAME_TM  = 0x04
SYNC_EOT  = 0xFF

APID_DEFAULT = 0x010


def connect(host=HOST, port=PORT, timeout=TIMEOUT, banner_wait=0.5):
    """Connect to Renode socket terminal; drain the ASCII boot banner."""
    sock = socket.create_connection((host, port), timeout=timeout)
    sock.settimeout(banner_wait)
    try:
        sock.recv(512)
    except socket.timeout:
        pass
    sock.settimeout(timeout)
    return sock


def build_tc(apid, svc, subsvc, data=b""):
    """Build a minimal PUS-C TC space packet (no TC frame wrapper needed for Renode)."""
    pkt_len = 6 + 5 + len(data)
    header = struct.pack(">HHH",
        0x1800 | (apid & 0x7FF),
        0xC000,
        pkt_len - 7,
    )
    secondary = bytes([0x20, svc, subsvc, 0x00, 0x00])
    return header + secondary + data


def build_ping(apid=APID_DEFAULT):
    return build_tc(apid, 17, 1)


def build_s8(fid, apid=APID_DEFAULT):
    """Build TC(8,1) — 2-byte BE function ID, zero args."""
    return build_tc(apid, 8, 1, struct.pack(">HB", fid, 0))


def build_s20_set(param_id, value_u32, apid=APID_DEFAULT):
    """Build TC(20,1) set parameter — param_id(2 BE) + value(4 BE)."""
    return build_tc(apid, 20, 1, struct.pack(">HI", param_id, value_u32))


def send_tc(sock, tc_bytes):
    """Send a TC wrapped in wire protocol v3 type-0x01 frame."""
    sock.sendall(bytes([FRAME_TC]) + struct.pack(">H", len(tc_bytes)) + tc_bytes)


def recv_tms(sock, wait=1.5, drain_timeout=0.5, bufsize=4096):
    """Sleep `wait` s then drain all available TM; return list of (svc, subsvc, body)."""
    time.sleep(wait)
    sock.settimeout(drain_timeout)
    raw = b""
    try:
        while True:
            chunk = sock.recv(bufsize)
            if not chunk:
                break
            raw += chunk
    except socket.timeout:
        pass
    return _parse(raw)


def _parse(raw):
    packets = []
    i = 0
    while i < len(raw):
        if raw[i] == SYNC_EOT:
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
            packets.append((body[7], body[8], body))
    return packets


def has_tm(packets, svc, subsvc):
    return any(s == svc and ss == subsvc for s, ss, _ in packets)


def describe(packets):
    labels = {
        (1,  1): "TM(1,1) acceptance-ok",
        (1,  2): "TM(1,2) acceptance-fail",
        (1,  7): "TM(1,7) completion-ok",
        (1,  8): "TM(1,8) completion-fail",
        (5,  1): "TM(5,1) event-info",
        (5,  4): "TM(5,4) event-high",
        (17, 2): "TM(17,2) pong",
        (20, 2): "TM(20,2) param-report",
    }
    return [labels.get((s, ss), f"TM({s},{ss})") for s, ss, _ in packets]
