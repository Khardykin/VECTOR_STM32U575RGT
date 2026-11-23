#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""retry_frames.py - re-send the 4 failing frames with independent CRC + retries."""
import serial, struct, time, sys

def crc16_v2(data):          # explicit if/else, no ternary
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc

def crc16_v1(data):          # ternary variant (as in send_frames.py)
    c = 0xFFFF
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if (c & 1) else c >> 1
    return c

def frame(slave, fn, addr, cnt):
    d = bytes([slave, fn, addr >> 8, addr & 0xFF, cnt >> 8, cnt & 0xFF])
    c = crc16_v2(d)
    return d + bytes([c & 0xFF, c >> 8])

def xfer(ser, req, idle_ms=20, total_ms=400):
    """Send one frame, read until the line is idle for idle_ms.

    read(300) with timeout=1 used to wait a full second per attempt,
    because a Modbus answer is only 5..165 bytes long.
    """
    ser.reset_input_buffer()
    t0 = time.time()
    ser.write(req)
    ser.flush()
    buf = bytearray()
    t_last = t0
    while (time.time() - t0) * 1000.0 < total_ms:
        b = ser.read(1)
        now = time.time()
        if b:
            buf += b
            t_last = now
        elif buf and (now - t_last) * 1000.0 >= idle_ms:
            break
    return bytes(buf)

port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
SLAVE = 100

# the 4 failing requests (as read/func frames)
tests = [
    ("hole 0x0000",   frame(SLAVE, 0x03, 0x0000, 1)),
    ("deveui 0x0036", frame(SLAVE, 0x03, 0x0036, 4)),
    ("hole 0x003A",   frame(SLAVE, 0x03, 0x003A, 1)),
    ("bad fn 0x08",   frame(SLAVE, 0x08, 0x0001, 1)),
]

# cross-check CRC implementations on all frames
print("== CRC cross-check ==")
for name, f in tests:
    body = f[:-2]
    v1, v2 = crc16_v1(body), crc16_v2(body)
    print("  %-14s v1=%04X v2=%04X %s" % (name, v1, v2, "MATCH" if v1 == v2 else "!!! DIFFER"))

ser = serial.Serial(port, 115200, timeout=0.005)

# prime the port: a few dummy bytes so UART receiver syncs
ser.write(b"\xFF\xFF\xFF"); ser.flush(); time.sleep(0.05)
ser.reset_input_buffer()

print("\n== retries ==")
for name, f in tests:
    got = None
    for attempt in range(1, 4):
        raw = xfer(ser, f)
        if raw:
            got = raw
            break
        time.sleep(0.05)
    if got:
        tag = ""
        if len(got) >= 3 and (got[1] & 0x80):
            tag = "  -> exception 0x%02X" % got[2]
        print("  %-14s attempt %d RX: %s%s" % (
            name, attempt, " ".join("%02X" % b for b in got[:24]), tag))
    else:
        print("  %-14s NO ANSWER after 3 attempts" % name)

ser.close()