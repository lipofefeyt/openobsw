#!/usr/bin/env python3
"""Live GY-273 / HMC5883L bench monitor for the WeAct STM32H750, via OpenOCD.

Drives I2C1 (PB8/PB9) directly through OpenOCD's TCL port while the firmware
keeps running, so you can check wiring and the sensor without reflashing.
Start OpenOCD in WSL2 first (hold RST while it connects, see docs/hardware.md):

    openocd -f interface/stlink.cfg -c "adapter speed 480" -f target/stm32h7x.cfg \
        -c "reset_config none separate" -c "bindto 0.0.0.0"

then, from the devcontainer:  python3 scripts/stm32h7_magmon.py 172.17.0.1 60
A working sensor prints ACK=['0x1e'], ID=b'H43' and |B| of roughly 25-65 uT.

Note: it briefly reconfigures PB8/PB9 each cycle; run it only while the
firmware is not using I2C (no magnetometer detected at boot), or expect the
AOCS to see occasional failed reads.

Each cycle:
  1. PB8/PB9 as inputs with internal pull-DOWN -> 1 means an external pull-up
     (module resistors) is present on that line.
  2. Restore I2C AF mode, probe 0x1E (and 0x0D, 0x2C); on ACK read ID + one sample.
usage: magmon.py <host> <seconds>
"""
import socket
import sys
import time

host, dur = sys.argv[1], float(sys.argv[2])
s = socket.create_connection((host, 6666), timeout=10)


def tcl(c):
    s.sendall(c.encode() + b"\x1a")
    b = b""
    while not b.endswith(b"\x1a"):
        b += s.recv(4096)
    return b[:-1].decode().strip()


def rd(a):
    return int(tcl(f"read_memory 0x{a:08x} 32 1"), 0)


def wr(a, v):
    tcl(f"write_memory 0x{a:08x} 32 0x{v:x}")


G = 0x58020400
MODER, PUPDR, IDR = G, G + 0x0C, G + 0x10
I2C = 0x40005400
CR2, ISR, ICR, RXDR, TXDR = I2C + 4, I2C + 0x18, I2C + 0x1C, I2C + 0x24, I2C + 0x28
START, AUTOEND, RDN = 1 << 13, 1 << 25, 1 << 10
m0, p0 = rd(MODER), rd(PUPDR)


def wait(mask, n=30):
    v = 0
    for _ in range(n):
        v = rd(ISR)
        if v & mask:
            break
    return v


def ack(a):
    wr(ICR, 0x3F38)
    wr(CR2, (a << 1) | AUTOEND | START)
    v = wait(1 << 5)
    wr(ICR, 0x3F38)
    return not (v & (1 << 4))


def read_regs(a, reg, n):
    wr(ICR, 0x3F38)
    wr(CR2, (a << 1) | (1 << 16) | START)
    if wait((1 << 1) | (1 << 4)) & (1 << 4):
        wr(ICR, 0x3F38)
        return None
    wr(TXDR, reg)
    wait(1 << 6)
    wr(CR2, (a << 1) | (n << 16) | RDN | AUTOEND | START)
    out = []
    for _ in range(n):
        v = wait((1 << 2) | (1 << 4))
        if v & (1 << 4):
            wr(ICR, 0x3F38)
            return None
        out.append(rd(RXDR) & 0xFF)
    wait(1 << 5)
    wr(ICR, 0x3F38)
    return out


def s16(h, l):
    v = (h << 8) | l
    return v - 65536 if v & 0x8000 else v


t0 = time.time()
configured = False
try:
    while time.time() - t0 < dur:
        # 1. external pull-up detection
        wr(MODER, m0 & ~(0xF << 16))
        wr(PUPDR, (p0 & ~(0xF << 16)) | (0xA << 16))
        v = rd(IDR)
        pu = ((v >> 8) & 1, (v >> 9) & 1)
        wr(PUPDR, p0)
        wr(MODER, m0)
        # 2. I2C
        line = f"t={time.time()-t0:5.1f}s  ext pull-up SCL,SDA={pu}"
        acks = [a for a in (0x1E, 0x0D, 0x2C) if ack(a)]
        line += f"  ACK={[hex(a) for a in acks] or 'none'}"
        if 0x1E in acks:
            idb = read_regs(0x1E, 0x0A, 3)
            line += f"  ID={bytes(idb) if idb else idb}"
            if not configured:
                for reg, val in ((0x00, 0x70), (0x01, 0x20), (0x02, 0x00)):
                    wr(ICR, 0x3F38)
                    wr(CR2, (0x1E << 1) | (2 << 16) | AUTOEND | START)
                    wait(1 << 1); wr(TXDR, reg); wait(1 << 1); wr(TXDR, val); wait(1 << 5)
                configured = True
            d = read_regs(0x1E, 0x03, 6)
            if d:
                x, z, y = s16(d[0], d[1]), s16(d[2], d[3]), s16(d[4], d[5])
                k = 100.0 / 1090.0  # LSB -> uT at 1.3 Ga gain
                line += f"  B[uT]=({x*k:+.1f},{y*k:+.1f},{z*k:+.1f}) |B|={(x*x+y*y+z*z)**0.5*k:.1f}"
        print(line, flush=True)
        time.sleep(0.5)
finally:
    wr(PUPDR, p0)
    wr(MODER, m0)
