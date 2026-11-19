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
ser = serial.Serial(port, 115200, timeout=1)
ser.reset_input_buffer()
ser.reset_output_buffer()

print(f"Порт: {port}")
print("Посылаю запрос 5 раз подряд...\n")

for attempt in range(5):
    req = struct.pack(">BBHH", 1, 0x03, 0x0000, 1)
    req += struct.pack("<H", crc16(req))
    
    ser.write(req)
    ser.flush()
    time.sleep(0.2)
    
    raw = ser.read(256)
    print(f"[{attempt+1}] TX: {' '.join(f'{b:02X}' for b in req)}")
    print(f"[{attempt+1}] RX: {' '.join(f'{b:02X}' for b in raw) if raw else '(пусто)'}")
    
    if len(raw) >= 2 and raw[0] == 1 and raw[1] == 3:
        print("    >>> ПОХОЖЕ НА ОТВЕТ!")
        bc = raw[2] if len(raw) > 2 else 0
        print(f"    >>> Байт-счётчик: {bc}")
        if len(raw) >= 3 + bc + 2:
            body = raw[3:3+bc+2]
            vals = [struct.unpack(">H", body[i*2:i*2+2])[0] for i in range(bc//2)]
            print(f"    >>> Значения: {vals}")
            break
    time.sleep(0.5)

print("\nПробую другие адреса...")
for slave_addr in [0, 2, 0xFF]:
    req = struct.pack(">BBHH", slave_addr, 0x03, 0x0000, 1)
    req += struct.pack("<H", crc16(req))
    ser.write(req)
    ser.flush()
    time.sleep(0.3)
    raw = ser.read(256)
    print(f"Адрес {slave_addr}: RX={' '.join(f'{b:02X}' for b in raw[:20]) if raw else '(пусто)'}")

ser.close()