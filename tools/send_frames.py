#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
send_frames.py - print ready-to-send Modbus frames for the Command_vector map.

  py tools/send_frames.py                 # print hex frames only
  py tools/send_frames.py COM3            # print AND send, show answers
  py tools/send_frames.py COM3 --write    # also test write/restore (safe regs)
"""
import struct, sys, time
try:
    import serial
except ImportError:
    serial = None

SLAVE = 1

def crc16(d):
    c = 0xFFFF
    for b in d:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if (c & 1) else c >> 1
    return c

def add_crc(b):
    c = crc16(b)
    return b + bytes([c & 0xFF, c >> 8])

def rd(fn, addr, cnt):
    return add_crc(bytes([SLAVE, fn, addr >> 8, addr & 0xFF, cnt >> 8, cnt & 0xFF]))

def wr6(addr, val):
    return add_crc(bytes([SLAVE, 0x06, addr >> 8, addr & 0xFF, val >> 8, val & 0xFF]))

def wr16(addr, vals):
    data = b"".join(struct.pack(">H", v) for v in vals)
    return add_crc(bytes([SLAVE, 0x10, addr >> 8, addr & 0xFF,
                          len(vals) >> 8, len(vals) & 0xFF, len(data)]) + data)

def hx(b):
    return " ".join("%02X" % x for x in b)

CASES = [
    ("link: hole 0x0000",            rd(0x03, 0x0000, 1),  "exc 02"),
    ("com block1 0x0001 x30",        rd(0x03, 0x0001, 30), "60 data bytes"),
    ("com block2 0x0020 x26",        rd(0x03, 0x0020, 26), "52 data bytes"),
    ("com block3 0x0040 x25",        rd(0x03, 0x0040, 25), "50 data bytes, flags=0"),
    ("Build_type 0x0001",            rd(0x03, 0x0001, 1),  "build id"),
    ("working_hours 0x0020 x2",      rd(0x03, 0x0020, 2),  "u32 Lo/Hi, ticks up"),
    ("archive ptrs 0x0026 x4",       rd(0x03, 0x0026, 4),  "Beggin/Current"),
    ("appkey 0x002E x8",             rd(0x03, 0x002E, 8),  "16 bytes"),
    ("deveui 0x0036 x4",             rd(0x03, 0x0036, 4),  "8 bytes"),
    ("in_map ALL 0x0001 x44",        rd(0x04, 0x0001, 44), "88 data bytes"),
    ("sensors T/H/P 0x0007 x6",      rd(0x04, 0x0007, 6),  "3 floats"),
    ("battery 0x0005 x2",            rd(0x04, 0x0005, 2),  "percent, mV"),
    ("counters 0x0015 x16",          rd(0x04, 0x0015, 16), "8 x u32"),
    ("hole 0x001F",                  rd(0x03, 0x001F, 1),  "exc 02"),
    ("hole 0x003A",                  rd(0x03, 0x003A, 1),  "exc 02"),
    ("hole 0x0059",                  rd(0x03, 0x0059, 1),  "exc 02"),
    ("hole 0x0F00",                  rd(0x03, 0x0F00, 1),  "exc 02"),
    ("bad function 0x08",            rd(0x08, 0x0001, 1),  "exc 01"),
    ("log read ptr0 x4",             rd(0x0C, 0x0000, 4),  "4*40 B or exc 02 if empty"),
    ("log find time",                add_crc(bytes([SLAVE, 0x14]) +
                                     struct.pack(">II", 0, 0xFFFFFFFF)), "ptr_from/to"),
]

WRITE_CASES = [
    ("wr6  TimeLedStatePeriod=100",  wr6(0x000D, 100),     "echo 6 bytes"),
    ("read back 0x000D",             rd(0x03, 0x000D, 1),  "100"),
    ("wr16 Immobility 0x0019 x3",    wr16(0x0019, [1, 60, 30]), "echo 6 bytes"),
    ("read back 0x0019 x3",          rd(0x03, 0x0019, 3),  "1, 60, 30"),
]

def main():
    args = [a for a in sys.argv[1:]]
    do_write = "--write" in args
    args = [a for a in args if not a.startswith("--")]
    port = args[0] if args else None

    cases = CASES + (WRITE_CASES if do_write else [])

    ser = None
    if port:
        if serial is None:
            print("pyserial missing: py -m pip install pyserial"); return 2
        ser = serial.Serial(port, 115200, timeout=1)
        print("port=%s 115200 8N1 slave=%d\n" % (port, SLAVE))

    for name, frame, expect in cases:
        print("%-30s TX: %-58s  expect: %s" % (name, hx(frame), expect))
        if ser:
            ser.reset_input_buffer()
            ser.write(frame); ser.flush()
            time.sleep(0.05)
            raw = ser.read(300)
            if not raw:
                print("%-30s RX: (empty)" % "")
            else:
                print("%-30s RX: %s" % ("", hx(raw[:24]) + (" ..." if len(raw) > 24 else "")))
                if len(raw) >= 3 and (raw[1] & 0x80):
                    print("%-30s     exception 0x%02X" % ("", raw[2]))
            print()

    if ser:
        ser.close()
    return 0

if __name__ == "__main__":
    sys.exit(main())
