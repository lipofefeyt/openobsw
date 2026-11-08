#!/usr/bin/env python3
"""Live status of a running STM32H750 openobsw image, read over SWD.

Reads memory through OpenOCD's TCL port without halting the CPU, so the
firmware (and its watchdog) keep running. Shows the LCD dashboard frame the
firmware last drew, the raw reset flags from boot and the watchdog count.

Start OpenOCD first (WSL2: alias stm32h7-ocd), then:

    python3 scripts/stm32h7_probe.py [--host H] [--elf build_stm32h7/obsw_stm32h7.elf]

--host defaults to $OBSW_OCD_HOST, else 127.0.0.1 (172.17.0.1 from the
devcontainer). The ELF must match the flashed image (symbol addresses).
"""

import argparse
import os
import socket
import subprocess
import sys

RSR_BITS = {
    17: "CPU", 19: "D1", 20: "D2", 21: "BOR", 22: "PIN", 23: "POR",
    24: "SFT", 26: "IWDG1", 28: "WWDG1", 30: "LPWR",
}


class Ocd:
    def __init__(self, host: str) -> None:
        try:
            self.s = socket.create_connection((host, 6666), timeout=5)
        except OSError as e:
            sys.exit(f"cannot reach OpenOCD TCL port at {host}:6666 ({e}). "
                     "Is the server running (alias stm32h7-ocd, in WSL2)?")

    def cmd(self, c: str) -> str:
        self.s.sendall(c.encode() + b"\x1a")
        buf = b""
        while not buf.endswith(b"\x1a"):
            buf += self.s.recv(4096)
        return buf[:-1].decode(errors="replace").strip()

    def read(self, addr: int, n_bytes: int) -> bytes:
        out = self.cmd(f"read_memory 0x{addr:08x} 8 {n_bytes}")
        try:
            return bytes(int(x, 0) for x in out.split())
        except ValueError:
            sys.exit(f"memory read failed ({out}). The target may have reset or "
                     "lost SWD; restart OpenOCD (hold RST while it connects).")

    def u32(self, addr: int) -> int:
        return int.from_bytes(self.read(addr, 4), "little")


def symbols(elf: str) -> dict:
    try:
        out = subprocess.run(["arm-none-eabi-nm", elf], capture_output=True,
                             text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        sys.exit(f"cannot read symbols from {elf}: {e}")
    sym = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3:
            sym[p[2]] = int(p[0], 16)
    return sym


def main() -> None:
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--host", default=os.environ.get("OBSW_OCD_HOST", "127.0.0.1"))
    ap.add_argument("--elf", default=os.path.join(repo, "build_stm32h7", "obsw_stm32h7.elf"))
    a = ap.parse_args()

    sym = symbols(a.elf)
    ocd = Ocd(a.host)
    state = ocd.cmd("targets").splitlines()[-1].split()[-1]
    print(f"target: {state}")

    if "s_boot_rcc_rsr" in sym:
        rsr = ocd.u32(sym["s_boot_rcc_rsr"])
        flags = [n for b, n in RSR_BITS.items() if rsr >> b & 1]
        print(f"boot RCC_RSR: 0x{rsr:08x}  {' '.join(flags) or '-'}")
    if "s_kick_count_hk" in sym:
        print(f"uptime / WDG kicks: {ocd.u32(sym['s_kick_count_hk'])} s")

    if "s_dash_frame" in sym:
        raw = ocd.read(sym["s_dash_frame"], 26 * 10)
        print("LCD dashboard (last frame drawn):")
        for r in range(10):
            print("  |" + raw[r * 26:(r + 1) * 26].decode("ascii", "replace") + "|")
    else:
        print("no s_dash_frame symbol: image predates the LCD dashboard?")


if __name__ == "__main__":
    main()
