import serial, time

s = serial.Serial('/dev/ttyUSB0', 115200, timeout=2)
s.write(b'\x00\x00\x00')   # type=0, hi=0, lo=0 → flen=0 → TMTC sends 0xFF back
time.sleep(0.5)
r = s.read(s.in_waiting)

print('got:', r.hex() if r else '(nothing)')
