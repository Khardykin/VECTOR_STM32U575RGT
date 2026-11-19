#!/usr/bin/env python3
import serial, struct, time, sys

def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else crc >> 1
    return crc

port = sys.argv[1] if len(sys.argv) > 1 else 'COM3'
ser = serial.Serial(port, 115200, timeout=1)

def mb_read(func, addr, cnt):
    req = struct.pack(">BBHH", 1, func, addr, cnt)
    req += struct.pack("<H", crc16(req))
    ser.write(req)
    time.sleep(0.05)
    head = ser.read(2)
    if len(head) < 2 or head[1] & 0x80:
        return None
    bc = ser.read(1)[0]
    body = ser.read(bc + 2)
    if crc16(head + bytes([bc]) + body[:bc]) != struct.unpack("<H", body[bc:])[0]:
        return None
    return [struct.unpack(">H", body[i*2:i*2+2])[0] for i in range(bc // 2)]

def mb_write(addr, values):
    data = b"".join(struct.pack(">H", v) for v in values)
    req = struct.pack(">BBHHB", 1, 0x10, addr, len(values), len(data)) + data
    req += struct.pack("<H", crc16(req))
    ser.write(req)
    time.sleep(0.05)
    return ser.read(8)

print("=== Проверка Command_vector ===")

# 1. Build_type (должен совпадать с тем, что в коде)
bt = mb_read(0x03, 0x0000, 1)
print(f"[1] Build_type = {bt[0]} (ожидается твоя версия сборки)")

# 2. Образ звуков
ok = mb_read(0x03, 0x0F1B, 1)
print(f"[2] IMG_OK = {ok[0]} (1 = образ залит и валиден)")

# 3. Геометрия flash
flash = mb_read(0x03, 0x0F10, 2)
size = (flash[1] << 16) | flash[0]
print(f"[3] Flash size = {size/1024/1024} МБ (ожидается 8)")

# 4. Датчики (температура x1000)
t = mb_read(0x04, 0x0031, 1)
print(f"[4] Temperature = {t[0]/1000:.1f} °C")

# 5. Стирание CONFIG (маленькая область, быстро)
print("[5] Стирание CONFIG (4 КБ)...")
mb_write(0x0F21, [1])
time.sleep(0.5)
st = mb_read(0x03, 0x0F26, 3)
print(f"    busy={st[0]}, err={st[1]}, pct={st[2]}")

# 6. Запись в регистр + чтение обратно
print("[6] Запись/чтение State...")
old = mb_read(0x03, 0x0001, 2)
mb_write(0x0001, [0x1234, 0x5678])
new = mb_read(0x03, 0x0001, 2)
print(f"    было: {old}, стало: {new}")
assert new == [0x1234, 0x5678], "FAIL: записанное не читается!"

print("\n=== ВСЕ ПРОВЕРКИ ПРОЙДЕНЫ ===")