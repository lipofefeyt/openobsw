"""renode/proto.py — Wire protocol v3 helpers for openobsw Renode tests.

Usage:
    import proto
    sock = proto.connect()
    proto.send_tc(sock, proto.build_ping())
    pkts = proto.recv_tms(sock, wait=1.5)
    assert proto.has_tm(pkts, 17, 2), "no pong"

    # Sensor / actuator (ZynqMP target only):
    proto.send_sensor(sock, q=[1,0,0,0], omega=[0,0,0], t=0.0, st_valid=1, gyro_valid=1)
    act, tms = proto.recv_tick(sock)
    assert act.controller == 1
"""
import socket
import struct
import time

HOST = "localhost"
PORT = 3456
TIMEOUT = 10.0

FRAME_TC     = 0x01
FRAME_SENSOR = 0x02
FRAME_ACT    = 0x03
FRAME_TM     = 0x04
SYNC_EOT     = 0xFF

_SENSOR_FMT = '<fffBffffBfffBf'   # 47 bytes LE — obsw_sensor_frame_t
_ACTUAT_FMT = '<ffffffBf'         # 29 bytes LE — obsw_actuator_frame_t

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


def build_sensor(q, omega, t, mag=None, mag_valid=0, st_valid=1, gyro_valid=1):
    """Build a 47-byte obsw_sensor_frame_t payload (little-endian)."""
    if mag is None:
        mag = (0.0, 0.0, 0.0)
    return struct.pack(_SENSOR_FMT,
        float(mag[0]),   float(mag[1]),   float(mag[2]),   int(mag_valid),
        float(q[0]),     float(q[1]),     float(q[2]),     float(q[3]),  int(st_valid),
        float(omega[0]), float(omega[1]), float(omega[2]), int(gyro_valid),
        float(t),
    )


def send_tc(sock, tc_bytes):
    """Send a TC wrapped in wire protocol v3 type-0x01 frame."""
    sock.sendall(bytes([FRAME_TC]) + struct.pack(">H", len(tc_bytes)) + tc_bytes)


def send_sensor(sock, q, omega, t, **kwargs):
    """Send a sensor frame (type 0x02) to a ZynqMP target."""
    payload = build_sensor(q, omega, t, **kwargs)
    sock.sendall(bytes([FRAME_SENSOR]) + struct.pack(">H", len(payload)) + payload)


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


class ActuatorFrame:
    """Parsed obsw_actuator_frame_t (29 bytes, little-endian)."""
    __slots__ = ['mtq', 'rw_torque', 'controller', 'sim_time']

    def __init__(self, data):
        v = struct.unpack(_ACTUAT_FMT, data)
        self.mtq        = v[0:3]   # magnetorquer dipoles [Am^2]
        self.rw_torque  = v[3:6]   # reaction wheel torques [N.m]
        self.controller = v[6]     # 0=bdot, 1=PD ADCS
        self.sim_time   = v[7]


def recv_tick(sock, timeout=8.0):
    """
    Read one complete sensor-tick response (up to and including 0xFF EOT).

    Returns (ActuatorFrame | None, tm_packets).
    Use after send_sensor() on ZynqMP targets.

    Timeout is generous (8 s default) because Renode emulation speed varies;
    the function returns as soon as the EOT sync byte arrives.
    """
    sock.settimeout(0.1)
    raw = b''
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            chunk = sock.recv(256)
            if chunk:
                raw += chunk
                if SYNC_EOT in raw:
                    break
        except socket.timeout:
            pass

    # Parser: skip bytes that are not a known frame type (e.g. boot banner).
    # FRAME_ACT=0x03 and FRAME_TM=0x04 are non-printable and won't appear in
    # ASCII banner text, so skipping unknown bytes is safe.
    act = None
    tms = []
    i = 0
    while i < len(raw):
        b = raw[i]
        if b == SYNC_EOT:
            break
        if b not in (FRAME_ACT, FRAME_TM):
            i += 1
            continue
        if i + 3 > len(raw):
            break
        flen = struct.unpack('>H', raw[i + 1:i + 3])[0]
        if i + 3 + flen > len(raw):
            i += 1
            continue
        payload = raw[i + 3:i + 3 + flen]
        i += 3 + flen
        if b == FRAME_ACT and len(payload) == 29:
            act = ActuatorFrame(payload)
        elif b == FRAME_TM and len(payload) >= 9:
            tms.append((payload[7], payload[8], payload))
    return act, tms


def _parse(raw):
    packets = []
    i = 0
    while i < len(raw):
        if raw[i] == SYNC_EOT:
            i += 1
            continue
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
