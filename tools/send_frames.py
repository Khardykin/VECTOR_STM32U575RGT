#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
send_frames.py - test Modbus frames for Command_vector register map.

  py tools/send_frames.py COM4                 # print AND send, show answers
  py tools/send_frames.py COM4 --write         # also test write/restore (safe regs)
  py tools/send_frames.py COM4 --broadcast     # use broadcast address 0 (write only)
  py tools/send_frames.py COM4 --slave 100     # custom slave address
  py tools/send_frames.py                      # print hex frames only (no sending)
"""
import struct, sys, time
try:
    import serial
except ImportError:
    serial = None

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

def verify_crc(frame):
    """Return True if CRC is valid."""
    if len(frame) < 3:
        return False
    expected = crc16(frame[:-2])
    actual = frame[-2] | (frame[-1] << 8)
    return expected == actual

def rd(fn, addr, cnt, slave):
    return add_crc(bytes([slave, fn, addr >> 8, addr & 0xFF, cnt >> 8, cnt & 0xFF]))

def wr6(addr, val, slave):
    return add_crc(bytes([slave, 0x06, addr >> 8, addr & 0xFF, val >> 8, val & 0xFF]))

def wr16(addr, vals, slave):
    data = b"".join(struct.pack(">H", v) for v in vals)
    return add_crc(bytes([slave, 0x10, addr >> 8, addr & 0xFF,
                          len(vals) >> 8, len(vals) & 0xFF, len(data)]) + data)

def hx(b):
    return " ".join("%02X" % x for x in b)

def xfer(ser, frame, idle_ms=15, total_ms=300):
    """Send one frame, read until the line is idle for idle_ms.

    Returns (raw, t_first_ms, t_total_ms).  t_first_ms is the real answer
    latency (first byte), t_total_ms includes the trailing idle window.
    An answer is normally 7..165 bytes, so reading a fixed 300 bytes with
    the port timeout (the old behaviour) added a fake ~50 ms to every row.
    """
    ser.reset_input_buffer()
    t0 = time.time()
    ser.write(frame)
    ser.flush()

    buf = bytearray()
    t_first = None
    t_last = t0

    while (time.time() - t0) * 1000.0 < total_ms:
        b = ser.read(1)
        now = time.time()
        if b:
            if t_first is None:
                t_first = (now - t0) * 1000.0
            buf += b
            t_last = now
        elif t_first is not None and (now - t_last) * 1000.0 >= idle_ms:
            break

    return bytes(buf), t_first, (time.time() - t0) * 1000.0

def make_cases(slave):
    cases = [
        ("link: hole 0x0000",            rd(0x03, 0x0000, 1, slave),  "exc 02"),
        ("com block1 0x0001 x30",        rd(0x03, 0x0001, 30, slave), "60 data bytes"),
        ("com block2 0x0020 x26",        rd(0x03, 0x0020, 26, slave), "52 data bytes"),
        ("com block3 0x0040 x25",        rd(0x03, 0x0040, 25, slave), "50 data bytes, flags=0"),
        ("Build_type 0x0001",            rd(0x03, 0x0001, 1, slave),  "build id"),
        ("working_hours 0x0020 x2",      rd(0x03, 0x0020, 2, slave),  "u32 Lo/Hi, ticks up"),
        ("archive ptrs 0x0026 x4",       rd(0x03, 0x0026, 4, slave),  "Beggin/Current"),
        ("appkey 0x002E x8",             rd(0x03, 0x002E, 8, slave),  "16 bytes"),
        ("deveui 0x0036 x4",             rd(0x03, 0x0036, 4, slave),  "8 bytes"),
        ("in_map ALL 0x0001 x44",        rd(0x04, 0x0001, 44, slave), "88 data bytes"),
        ("sensors T/H/P 0x0007 x6",      rd(0x04, 0x0007, 6, slave),  "3 floats"),
        ("battery 0x0005 x2",            rd(0x04, 0x0005, 2, slave),  "percent, mV"),
        ("counters 0x0015 x16",          rd(0x04, 0x0015, 16, slave), "8 x u32"),
        ("hole 0x001F",                  rd(0x03, 0x001F, 1, slave),  "exc 02"),
        ("hole 0x003A",                  rd(0x03, 0x003A, 1, slave),  "exc 02"),
        ("hole 0x0059",                  rd(0x03, 0x0059, 1, slave),  "exc 02"),
        ("hole 0x0F00",                  rd(0x03, 0x0F00, 1, slave),  "exc 02"),
        ("bad function 0x08",            rd(0x08, 0x0001, 1, slave),  "exc 01"),
        ("log read ptr0 x4",             rd(0x0C, 0x0000, 4, slave),  "4*40 B or exc 02 if empty"),
        ("log find time",                add_crc(bytes([slave, 0x14]) +
                                         struct.pack(">II", 0, 0xFFFFFFFF)), "ptr_from/to"),
    ]
    
    write_cases = [
        ("wr6  TimeLedStatePeriod=100",  wr6(0x000D, 100, slave),     "echo 6 bytes"),
        ("read back 0x000D",             rd(0x03, 0x000D, 1, slave),  "100"),
        ("wr16 Immobility 0x0019 x3",    wr16(0x0019, [1, 60, 30], slave), "echo 6 bytes"),
        ("read back 0x0019 x3",          rd(0x03, 0x0019, 3, slave),  "1, 60, 30"),
    ]
    
    return cases, write_cases

def main():
    args = sys.argv[1:]
    do_write = "--write" in args
    broadcast = "--broadcast" in args
    
    # Parse --slave N
    slave = 100  # default
    if "--slave" in args:
        idx = args.index("--slave")
        if idx + 1 < len(args):
            try:
                slave = int(args[idx + 1])
                args = args[:idx] + args[idx+2:]  # remove --slave and value
            except ValueError:
                print("Error: --slave requires numeric value"); return 2
        else:
            print("Error: --slave requires value"); return 2
    
    # Remove flags from args
    args = [a for a in args if not a.startswith("--")]
    
    port = args[0] if args else None
    
    if broadcast:
        slave = 0
        print("BROADCAST MODE: slave=0 (no response expected for reads)")
    
    cases, write_cases = make_cases(slave)
    cases = cases + (write_cases if do_write else [])

    ser = None
    if port:
        if serial is None:
            print("pyserial missing: py -m pip install pyserial"); return 2
        # Small timeout: xfer() polls one byte at a time and times the first
        # byte.  With timeout=0.05 every read(1) could block up to 50 ms.
        ser = serial.Serial(port, 115200, timeout=0.005)
        print("port=%s 115200 8N1 slave=%d\n" % (port, slave))

    for name, frame, expect in cases:
        # Verify TX CRC
        tx_crc_ok = verify_crc(frame)
        print("%-30s TX: %-58s  expect: %s  CRC: %s" % (
            name, hx(frame), expect, "OK" if tx_crc_ok else "BAD"))
        
        if ser:
            raw, t_first, t_total = xfer(ser, frame)
            retries = 0

            # On this USB-RS485 link roughly one frame in ten is lost even
            # when the device answers every solo request (20/20).  One retry
            # makes the run deterministic: 20/20 with a single resend.
            while (not broadcast) and ((not raw) or (not verify_crc(raw))) and (retries < 2):
                retries += 1
                time.sleep(0.005)
                raw, t_first, t_total = xfer(ser, frame)
            
            if not raw:
                if broadcast:
                    print("%-30s RX: (no response - broadcast)  wait=%.1f ms" % ("", t_total))
                else:
                    print("%-30s RX: (empty)  wait=%.1f ms  retry=%d" % ("", t_total, retries))
            else:
                rx_crc_ok = verify_crc(raw)
                print("%-30s RX: %s  first=%.1f ms  total=%.1f ms  retry=%d  CRC: %s" % (
                    "", hx(raw[:24]) + (" ..." if len(raw) > 24 else ""),
                    t_first, t_total, retries, "OK" if rx_crc_ok else "BAD"))
                if len(raw) >= 3 and (raw[1] & 0x80):
                    print("%-30s     exception 0x%02X" % ("", raw[2]))
            print()

    if ser:
        ser.close()
    return 0

if __name__ == "__main__":
    sys.exit(main())