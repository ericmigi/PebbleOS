#!/usr/bin/env python3
"""Power-cycle via ppk2d and capture UART boot output. Never touches PPK2 directly."""
import serial, sys, time, urllib.request
baud = int(sys.argv[1]) if len(sys.argv) > 1 else 1000000
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 8
s = serial.Serial("/dev/cu.usbmodem5B7A1354731", baud, timeout=0.3)
s.rts = False
s.reset_input_buffer()
urllib.request.urlopen("http://127.0.0.1:8843/cycle?off_ms=1200", timeout=15).read()
buf = b""
end = time.time() + secs
while time.time() < end:
    buf += s.read(4096)
print(buf.decode("utf-8", "replace"))
