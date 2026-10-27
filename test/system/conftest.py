"""
System tests: drive the real obsw_sim binary over wire protocol v3.

The binary is taken from $OBSW_SIM, else build/sim/obsw_sim. Each test
builds one input stream (TC and sensor frames), runs the sim to EOF and
inspects the ordered output.
"""

from __future__ import annotations

import os
import struct
import subprocess
from dataclasses import dataclass, field
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]

FRAME_TC = 0x01
FRAME_SENSOR = 0x02
FRAME_ACTUATOR = 0x03
FRAME_TM = 0x04
END_OF_TICK = 0xFF

# HK set IDs and parameter IDs used by the tests (srdb/data)
HK_DHS_OBC = 3
HK_AOCS = 4
PARAM_OBC_UPTIME = 0x0004
PARAM_BDOT_GAIN = 0x20A0

# TM(1,2) failure codes (include/obsw/pus/s1.h)
FAIL_ILLEGAL_SVC = 0x0002
FAIL_ILLEGAL_SUBSVC = 0x0003
FAIL_ILLEGAL_APP_DATA = 0x0005
FAIL_MEM_ACCESS = 0x0006
FAIL_MODE_GATED = 0x0007

# obc_mode enumeration (srdb/data/parameters.yaml)
MODE_STANDBY, MODE_SAFE, MODE_NOMINAL = 0, 1, 2


def pytest_configure(config: pytest.Config) -> None:
    config.addinivalue_line("markers", "requirement(id): SRS requirement verified by the test")


@dataclass
class Tm:
    service: int
    subservice: int
    app: bytes

    @property
    def tc_seq(self) -> int:
        """Sequence count of the TC a TM(1,x) report refers to."""
        return struct.unpack(">H", self.app[2:4])[0] & 0x3FFF

    @property
    def failure_code(self) -> int:
        return struct.unpack(">H", self.app[4:6])[0]


@dataclass
class Output:
    """Ordered sim output: ('tm', Tm), ('act', bytes) or ('eot', None)."""
    events: list[tuple[str, object]] = field(default_factory=list)

    @property
    def tms(self) -> list[Tm]:
        return [e for k, e in self.events if k == "tm"]  # type: ignore[misc]

    def s1_for(self, seq: int) -> list[Tm]:
        return [t for t in self.tms if t.service == 1 and t.tc_seq == seq]

    def hk(self, set_id: int) -> list[bytes]:
        return [t.app[1:] for t in self.tms
                if t.service == 3 and t.subservice == 25 and t.app[0] == set_id]


class Stream:
    """Builds a wire protocol v3 input stream."""

    def __init__(self) -> None:
        self.data = b""
        self.seq = 0
        self.sim_time = 0.0

    def tc(self, svc: int, sub: int, app: bytes = b"", apid: int = 0x010) -> int:
        """Append a PUS-C TC; returns its sequence count."""
        self.seq += 1
        body = bytes([0x11, svc, sub, 0x00, 0x00]) + app
        pkt = struct.pack(">HHH", 0x1800 | apid, 0xC000 | self.seq, len(body) - 1) + body
        self.data += bytes([FRAME_TC]) + struct.pack(">H", len(pkt)) + pkt
        return self.seq

    def ticks(self, n: int, dt: float = 0.1) -> None:
        """Append n sensor frames (all sensors invalid), dt seconds apart."""
        for _ in range(n):
            self.sim_time += dt
            body = bytes(13 + 17 + 13) + struct.pack("<f", self.sim_time)
            self.data += bytes([FRAME_SENSOR]) + struct.pack(">H", len(body)) + body

    def nominal(self) -> int:
        """TC(8,1) function 1: recover to NOMINAL."""
        return self.tc(8, 1, struct.pack(">HB", 1, 0))


def parse(out: bytes) -> Output:
    res, i = Output(), 0
    while i < len(out):
        kind = out[i]
        if kind == END_OF_TICK:
            res.events.append(("eot", None))
            i += 1
            continue
        n = struct.unpack(">H", out[i + 1:i + 3])[0]
        body = out[i + 3:i + 3 + n]
        i += 3 + n
        if kind == FRAME_TM:
            res.events.append(("tm", Tm(body[7], body[8], body[17:])))
        elif kind == FRAME_ACTUATOR:
            res.events.append(("act", body))
        else:
            raise AssertionError(f"unexpected frame type 0x{kind:02X} from obsw_sim")
    return res


@pytest.fixture(scope="session")
def sim_path() -> Path:
    explicit = os.environ.get("OBSW_SIM")
    path = Path(explicit or REPO / "build" / "sim" / "obsw_sim")
    if not path.exists():
        # CI sets OBSW_SIM, so a missing binary there is a failure, not a skip
        if explicit:
            pytest.fail(f"OBSW_SIM={explicit} does not exist")
        pytest.skip(f"obsw_sim not built at {path} (set OBSW_SIM)")
    return path


@pytest.fixture
def run(sim_path: Path):
    def _run(stream: Stream) -> Output:
        proc = subprocess.run([str(sim_path)], input=stream.data,
                              capture_output=True, timeout=20, check=True)
        return parse(proc.stdout)
    return _run
