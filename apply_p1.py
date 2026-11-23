#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_p2.py — Patch 2: Audio reload + test scripts
Run from project root:  py apply_p2.py

Изменения:
1. Добавляет регистр audio_reload_pending (0x0059) в com_map
2. Добавляет поле audio_reload_pending в cmd_state_t
3. Добавляет вызов audio_reload_image() в Command_vector_ExecutePendingActions
4. Создаёт tools/test_log_read.py для чтения журнала
5. Создаёт tools/test_flash_write.py для записи файлов во flash
"""
import os, sys, re, shutil
from pathlib import Path

ROOT = Path(".")
BAK = ROOT / "_backup_p2"
REPORT = []

def log(msg):
    REPORT.append(msg)
    print(msg)

def backup(p: Path):
    rel = p.relative_to(ROOT)
    dst = BAK / rel
    dst.parent.mkdir(parents=True, exist_ok=True)
    if not dst.exists():
        shutil.copy2(p, dst)

def read(p: Path) -> str:
    return p.read_text(encoding="utf-8", errors="replace")

def write(p: Path, s: str):
    p.write_text(s, encoding="utf-8")

def patch_command_vector_h():
    p = ROOT / "Core" / "VectorLib" / "Inc" / "Command_vector.h"
    if not p.exists():
        log(f"[SKIP] {p}"); return
    backup(p)
    src = read(p)
    
    if "audio_reload_pending" not in src:
        src = src.replace(
            "volatile uint8_t ext_write_pending;",
            "volatile uint8_t ext_write_pending;\n  volatile uint8_t audio_reload_pending;"
        )
        log("  [OK] добавлено поле audio_reload_pending в cmd_state_t")
        write(p, src)
    else:
        log("  [ALREADY] audio_reload_pending уже есть в cmd_state_t")
    
    log(f"  [DONE] {p}")

def patch_command_vector_c():
    p = ROOT / "Core" / "VectorLib" / "Src" / "Command_vector.c"
    if not p.exists():
        log(f"[SKIP] {p}"); return
    backup(p)
    src = read(p)
    changed = False
    
    # Добавляем регистр
    if "audio_reload_pending" not in src:
        src = src.replace(
            "CS(block_sound,               0x0058, T_U8),",
            "CS(block_sound,               0x0058, T_U8),\n  CS(audio_reload_pending,       0x0059, T_U8),"
        )
        log("  [OK] добавлен регистр audio_reload_pending (0x0059)")
        changed = True
    else:
        log("  [ALREADY] регистр audio_reload_pending уже есть")
    
    # Добавляем обработку
    if "audio_reload_image()" not in src:
        # Ищем конец функции Command_vector_ExecutePendingActions
        marker = "? CV_ST_OK : CV_ST_VERIFY;\n    return;\n  }\n}"
        replacement = """? CV_ST_OK : CV_ST_VERIFY;
    return;
  }

  /* --- audio reload image --------------------------------------------- */
  if (cmd_state.audio_reload_pending != 0u)
  {
    cmd_state.audio_reload_pending = 0u;
    audio_reload_image();
    cmd_state.flash_status = CV_ST_OK;
    return;
  }
}"""
        src = src.replace(marker, replacement)
        log("  [OK] добавлена обработка audio_reload_pending")
        changed = True
    else:
        log("  [ALREADY] обработка audio_reload_pending уже есть")
    
    if changed:
        write(p, src)
    log(f"  [DONE] {p}")

def create_test_log_read():
    script = '''#!/usr/bin/env python3
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
    print(f"\\nReceived {rx_cnt} records:")
    print("="*70)
    for i in range(rx_cnt):
        rec = parse_log_record(raw, 3 + i * 40)
        if rec:
            print(f"\\nRecord {i+1}:")
            print(f"  Type: 0x{rec['type']:02X}, Fmt: {rec['fmt_ver']}, Payload: {rec['payload_len']} bytes")
            print(f"  Working hours: {rec['working_hours']} sec ({rec['working_hours']/3600:.1f} h)")
            print(f"  Current addr: {rec['current_addr']}")
            print(f"  Payload: {' '.join('%02X' % b for b in rec['payload'])}")
    ser.close()

if __name__ == "__main__":
    main()
'''
    p = ROOT / "tools" / "test_log_read.py"
    p.parent.mkdir(parents=True, exist_ok=True)
    write(p, script)
    log(f"  [OK] создан {p}")

def create_test_flash_write():
    script = '''#!/usr/bin/env python3
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
    print("\\n1. Блокировка звука (block_sound = 1)...")
    if not modbus_write_single(ser, slave, 0x0058, 1):
        print("Failed"); ser.close(); return
    print("  OK")
    print("\\n2. Установка flash_key...")
    key_hi, key_lo = 0xDEAD, 0xBEEF
    if not modbus_write_single(ser, slave, 0x0046, key_hi):
        print("Failed"); ser.close(); return
    if not modbus_write_single(ser, slave, 0x0045, key_lo):
        print("Failed"); ser.close(); return
    print(f"  OK (key=0x{key_hi:04X}{key_lo:04X})")
    print("\\n3. Установка flash_target...")
    if not modbus_write_single(ser, slave, 0x0040, target_val):
        print("Failed"); ser.close(); return
    print(f"  OK (target={target_val})")
    print("\\n4. Установка flash_len...")
    len_hi = (file_size >> 16) & 0xFFFF
    len_lo = file_size & 0xFFFF
    if not modbus_write_single(ser, slave, 0x0044, len_hi):
        print("Failed"); ser.close(); return
    if not modbus_write_single(ser, slave, 0x0043, len_lo):
        print("Failed"); ser.close(); return
    print(f"  OK (len={file_size})")
    print("\\n5. Отправка чанков...")
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
    print("\\n6. Проверка завершения...")
    pending = modbus_read_single(ser, slave, 0x004F)
    if pending is None:
        print("  Failed to read ext_write_pending")
    elif pending != 0:
        print(f"  ext_write_pending = {pending} (expected 0)")
    else:
        print("  OK")
    print("\\n7. Перезагрузка образа звука (audio_reload_pending = 1)...")
    if not modbus_write_single(ser, slave, 0x0059, 1):
        print("  Failed")
    else:
        print("  OK")
    print("\\n8. Разблокировка звука (block_sound = 0)...")
    if not modbus_write_single(ser, slave, 0x0058, 0):
        print("  Failed")
    else:
        print("  OK")
    ser.close()
    print("\\n=== DONE ===")

if __name__ == "__main__":
    main()
'''
    p = ROOT / "tools" / "test_flash_write.py"
    p.parent.mkdir(parents=True, exist_ok=True)
    write(p, script)
    log(f"  [OK] создан {p}")

def main():
    print("=" * 72)
    print("  PATCH 2: Audio reload + test scripts")
    print(f"  Backup: {BAK.resolve()}")
    print("=" * 72)
    print()

    patch_command_vector_h()
    print()
    patch_command_vector_c()
    print()
    create_test_log_read()
    print()
    create_test_flash_write()

    print()
    print("=" * 72)
    print("  ИТОГ:")
    oks = [r for r in REPORT if "[OK]" in r]
    already = [r for r in REPORT if "[ALREADY]" in r]
    print(f"  Применено новых изменений: {len(oks)}")
    print(f"  Уже было применено ранее:  {len(already)}")
    print()
    print("  Дальше:")
    print("    1. Пересобери прошивку в STM32CubeIDE")
    print("    2. py tools/test_log_read.py COM4 --slave 100 --ptr 0 --count 5")
    print("    3. py tools/test_flash_write.py COM4 --slave 100 --file tools/sounds.bin --target sound")
    print("=" * 72)

if __name__ == "__main__":
    main()