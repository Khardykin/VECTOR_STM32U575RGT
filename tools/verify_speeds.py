#!/usr/bin/env python3
import serial, struct, time, sys

def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else crc >> 1
    return crc

port = sys.argv[1] if len(sys.argv) > 1 else 'COM4'
speeds = [9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600]

req = struct.pack(">BBHH", 1, 0x03, 0x0000, 1)
req += struct.pack("<H", crc16(req))

print(f"Порт: {port}")
print(f"Запрос: {' '.join(f'{b:02X}' for b in req)}\n")

for baud in speeds:
    try:
        ser = serial.Serial(port, baud, timeout=0.5)
        ser.reset_input_buffer()
        ser.write(req)
        ser.flush()
        time.sleep(0.2)
        raw = ser.read(256)
        ser.close()
        
        status = "✓ ПОХОЖЕ НА ОТВЕТ!" if (len(raw) >= 3 and raw[0] == 1 and raw[1] == 3) else ""
        print(f"{baud:>6} бод: {' '.join(f'{b:02X}' for b in raw[:20]) if raw else '(пусто)':<60} {status}")
    except Exception as e:
        print(f"{baud:>6} бод: ОШИБКА {e}")