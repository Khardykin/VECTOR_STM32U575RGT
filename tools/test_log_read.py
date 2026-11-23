#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_log_read.py - чтение журнала через команду 0x0C"""
import serial, struct, sys, time

def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else crc >> 1
    return crc

def make_frame_log_read(slave, ptr, cnt):
    data = struct.pack(">BBHH", slave, 0x0C, ptr, cnt)
    crc = crc16(data)
    return data + struct.pack("<H", crc)

def parse_log_record(data, offset):
    if len(data) < offset + 40:
        return None
    rec = data[offset:offset+40]
    return {
        'type': rec[0], 'fmt_ver': rec[1], 'payload_len': rec[2], 'flags': rec[3],
        'working_hours': struct.unpack("<I", rec[4:8])[0],
        'current_addr': struct.unpack("<I", rec[8:12])[0],
        'payload': rec[12:12+rec[2]]
    }

def main():
    if len(sys.argv) < 2:
        print("Usage: py test_log_read.py COM4 [--slave 100] [--ptr 0] [--count 5]")
        return
    port = sys.argv[1]
    slave, ptr, count = 100, 0, 5
    i = 2
    while i < len(sys.argv):
        if sys.argv[i] == '--slave' and i+1 < len(sys.argv):
            slave = int(sys.argv[i+1]); i += 2
        elif sys.argv[i] == '--ptr' and i+1 < len(sys.argv):
            ptr = int(sys.argv[i+1]); i += 2
        elif sys.argv[i] == '--count' and i+1 < len(sys.argv):
            count = int(sys.argv[i+1]); i += 2
        else:
            i += 1
    print(f"Port: {port}, Slave: {slave}, Ptr: {ptr}, Count: {count}")
    ser = serial.Serial(port, 115200, timeout=1)
    req = make_frame_log_read(slave, ptr, count)
    print(f"TX: {' '.join('%02X' % b for b in req)}")
    ser.write(req); ser.flush()
    time.sleep(0.1)
    raw = ser.read(3000)
    if not raw:
        print("No answer!"); ser.close(); return
    print(f"RX ({len(raw)} bytes): {' '.join('%02X' % b for b in raw[:min(100, len(raw))])}")
    if len(raw) < 5:
        print("Answer too short!"); ser.close(); return
    rx_crc = struct.unpack("<H", raw[-2:])[0]
    calc_crc = crc16(raw[:-2])
    if rx_crc != calc_crc:
        print(f"CRC mismatch: RX=0x{rx_crc:04X}, calc=0x{calc_crc:04X}"); ser.close(); return
    if raw[0] != slave or (raw[1] & 0x80):
        print(f"Error response: func=0x{raw[1]:02X}")
        if len(raw) >= 3:
            print(f"Exception code: 0x{raw[2]:02X}")
        ser.close(); return
    rx_cnt = raw[2]
    expected_len = 3 + rx_cnt * 40 + 2
    if len(raw) < expected_len:
        print(f"Answer too short: {len(raw)} < {expected_len}"); ser.close(); return
    print(f"\nReceived {rx_cnt} records:")
    print("="*70)
    for i in range(rx_cnt):
        rec = parse_log_record(raw, 3 + i * 40)
        if rec:
            print(f"\nRecord {i+1}:")
            print(f"  Type: 0x{rec['type']:02X}, Fmt: {rec['fmt_ver']}, Payload: {rec['payload_len']} bytes")
            print(f"  Working hours: {rec['working_hours']} sec ({rec['working_hours']/3600:.1f} h)")
            print(f"  Current addr: {rec['current_addr']}")
            print(f"  Payload: {' '.join('%02X' % b for b in rec['payload'])}")
    ser.close()

if __name__ == "__main__":
    main()
