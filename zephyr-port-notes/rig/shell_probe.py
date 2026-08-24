#!/usr/bin/env python3
"""Cycle power, wait for boot, drive the Zephyr shell, print transcript."""
import serial, sys, time, urllib.request
s = serial.Serial("/dev/cu.usbmodem5B7A1354731", 1000000, timeout=0.3)
s.rts = False
s.reset_input_buffer()
urllib.request.urlopen("http://127.0.0.1:8843/cycle?off_ms=1200", timeout=15).read()
time.sleep(2.5)
buf = s.read(65536)
for cmd in sys.argv[1:]:
    s.write(cmd.encode() + b"\r\n"); s.flush()
    time.sleep(0.8)
    buf += s.read(65536)
print(buf.decode("utf-8", "replace"))
