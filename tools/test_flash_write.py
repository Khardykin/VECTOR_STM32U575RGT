#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_flash_write.py - запись файла во внешнюю flash"""
import serial, struct, sys, time, os

def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else crc >> 1
    return crc

def modbus_write_single(ser, slave, addr, value):
    req = struct.pack(">BBHH", slave, 0x06, addr, value)
    req += struct.pack("<H", crc16(req))
    ser.write(req); ser.flush()
    time.sleep(0.05)
    resp = ser.read(8)
    if len(resp) < 8:
        return False
    rx_crc = struct.unpack("<H", resp[-2:])[0]
    if crc16(resp[:-2]) != rx_crc:
        return False
    return resp[0] == slave and resp[1] == 0x06

def modbus_read_single(ser, slave, addr):
    req = struct.pack(">BBHH", slave, 0x03, addr, 1)
    req += struct.pack("<H", crc16(req))
    ser.write(req); ser.flush()
    time.sleep(0.05)
    resp = ser.read(7)
    if len(resp) < 7:
        return None
    rx_crc = struct.unpack("<H", resp[-2:])[0]
    if crc16(resp[:-2]) != rx_crc:
        return None
    if resp[0] != slave or (resp[1] & 0x80):
        return None
    return struct.unpack(">H", resp[3:5])[0]

def write_chunk(ser, slave, addr, data):
    if len(data) > 256:
        print(f"Chunk too large: {len(data)} > 256")
        return False
    req = struct.pack(">BBI", slave, 0x0F, addr)
    req += bytes([len(data)])
    req += data
    req += struct.pack("<H", crc16(req))
    ser.write(req); ser.flush()
    time.sleep(0.1)
    resp = ser.read(100)
    if len(resp) < 8:
        print(f"No response or too short: {len(resp)}")
        return False
    rx_crc = struct.unpack("<H", resp[-2:])[0]
    if crc16(resp[:-2]) != rx_crc:
        print(f"CRC mismatch")
        return False
    if resp[1] & 0x80:
        print(f"Error response: exception 0x{resp[2]:02X}")
        return False
    return True

def main():
    if len(sys.argv) < 2:
        print("Usage: py test_flash_write.py COM4 [--slave 100] [--file sounds.bin] [--target sound|fw]")
        return
    port = sys.argv[1]
    slave = 100
    filename = None
    target = "sound"
    i = 2
    while i < len(sys.argv):
        if sys.argv[i] == '--slave' and i+1 < len(sys.argv):
            slave = int(sys.argv[i+1]); i += 2
        elif sys.argv[i] == '--file' and i+1 < len(sys.argv):
            filename = sys.argv[i+1]; i += 2
        elif sys.argv[i] == '--target' and i+1 < len(sys.argv):
            target = sys.argv[i+1]; i += 2
        else:
            i += 1
    if not filename:
        print("Error: --file required"); return
    if not os.path.exists(filename):
        print(f"File not found: {filename}"); return
    file_size = os.path.getsize(filename)
    target_val = 1 if target == "sound" else 2
    print(f"Port: {port}, Slave: {slave}")
    print(f"File: {filename} ({file_size} bytes)")
    print(f"Target: {target} ({target_val})")
    ser = serial.Serial(port, 115200, timeout=1)
    print("\n1. Блокировка звука (block_sound = 1)...")
    if not modbus_write_single(ser, slave, 0x0058, 1):
        print("Failed"); ser.close(); return
    print("  OK")
    print("\n2. Установка flash_key...")
    key_hi, key_lo = 0xDEAD, 0xBEEF
    if not modbus_write_single(ser, slave, 0x0046, key_hi):
        print("Failed"); ser.close(); return
    if not modbus_write_single(ser, slave, 0x0045, key_lo):
        print("Failed"); ser.close(); return
    print(f"  OK (key=0x{key_hi:04X}{key_lo:04X})")
    print("\n3. Установка flash_target...")
    if not modbus_write_single(ser, slave, 0x0040, target_val):
        print("Failed"); ser.close(); return
    print(f"  OK (target={target_val})")
    print("\n4. Установка flash_len...")
    len_hi = (file_size >> 16) & 0xFFFF
    len_lo = file_size & 0xFFFF
    if not modbus_write_single(ser, slave, 0x0044, len_hi):
        print("Failed"); ser.close(); return
    if not modbus_write_single(ser, slave, 0x0043, len_lo):
        print("Failed"); ser.close(); return
    print(f"  OK (len={file_size})")
    print("\n5. Отправка чанков...")
    with open(filename, "rb") as f:
        offset, chunk_num = 0, 0
        while offset < file_size:
            chunk = f.read(256)
            chunk_num += 1
            print(f"  Chunk {chunk_num}: offset={offset}, size={len(chunk)}")
            if not write_chunk(ser, slave, offset, chunk):
                print(f"  FAILED at offset {offset}"); ser.close(); return
            status = modbus_read_single(ser, slave, 0x0047)
            if status is None:
                print("  Failed to read flash_status"); ser.close(); return
            if status != 0:
                print(f"  flash_status = {status} (expected 0)"); ser.close(); return
            time.sleep(0.05)
            offset += len(chunk)
    print("\n6. Проверка завершения...")
    pending = modbus_read_single(ser, slave, 0x004F)
    if pending is None:
        print("  Failed to read ext_write_pending")
    elif pending != 0:
        print(f"  ext_write_pending = {pending} (expected 0)")
    else:
        print("  OK")
    print("\n7. Перезагрузка образа звука (audio_reload_pending = 1)...")
    if not modbus_write_single(ser, slave, 0x0059, 1):
        print("  Failed")
    else:
        print("  OK")
    print("\n8. Разблокировка звука (block_sound = 0)...")
    if not modbus_write_single(ser, slave, 0x0058, 0):
        print("  Failed")
    else:
        print("  OK")
    ser.close()
    print("\n=== DONE ===")

if __name__ == "__main__":
    main()
