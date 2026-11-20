#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""diag_badfn.py - deep probe of unknown-function handling (expect exc 0x01)."""
import serial, struct, time, sys

def crc16(d):
    c = 0xFFFF
    for b in d:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if (c & 1) else c >> 1
    return c

def frame(slave, fn):
    d = bytes([slave, fn, 0x00, 0x01, 0x00, 0x01])
    c = crc16(d)
    return d + bytes([c & 0xFF, c >> 8])

port  = sys.argv[1] if len(sys.argv) > 1 else "COM4"
SLAVE = 100

ser = serial.Serial(port, 115200, timeout=1)

# prime the port so the first frame is not eaten by UART sync
ser.write(b"\xFF\xFF\xFF"); ser.flush(); time.sleep(0.1)
ser.reset_input_buffer()

def probe(fn, listen_ms=2500):
    f = frame(SLAVE, fn)
    ser.reset_input_buffer()
    ser.write(f); ser.flush()
    raw = b""
    t_end = time.time() + listen_ms / 1000.0
    while time.time() < t_end:
        raw += ser.read(64)
        if raw:
            break
        time.sleep(0.02)
    print("fn 0x%02X  TX: %s" % (fn, " ".join("%02X" % b for b in f)))
    if raw:
        tag = ""
        if len(raw) >= 3 and (raw[1] & 0x80):
            tag = "   -> exception 0x%02X%s" % (raw[2],
                  "  (ILLEGAL FUNCTION)" if raw[2] == 0x01 else "")
        print("          RX: %s%s" % (" ".join("%02X" % b for b in raw[:16]), tag))
    else:
        print("          RX: (silent for %d ms)" % listen_ms)
    print()

print("== baseline: known-good read 0x0001 ==")
d = bytes([SLAVE, 0x03, 0x00, 0x01, 0x00, 0x01])
d += struct.pack("<H", crc16(d))
ser.reset_input_buffer(); ser.write(d); ser.flush()
time.sleep(0.2)
print("RX:", " ".join("%02X" % b for b in ser.read(32)), "\n")

print("== unknown functions (expect 64 88 01 ..) ==")
for fn in (0x05, 0x08, 0x2B):
    probe(fn)

ser.close()
print("If ALL unknown fns are silent -> flashed firmware lacks the default")
print("exception path: rebuild latest sources and reflash.")
print("If only 0x08 is silent -> frame-specific issue, send this output back.")