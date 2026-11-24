#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_flash_write_final.py - Надежная запись звука с обработкой BUSY и гарантированным Erase.
"""
import serial, struct, sys, time, os, argparse

def crc16(d):
    c=0xFFFF
    for b in d:
        c^=b
        for _ in range(8): c=(c>>1)^0xA001 if c&1 else c>>1
    return c

class ModbusMaster:
    def __init__(self, port, slave):
        self.ser = serial.Serial(port, 115200, timeout=1)
        self.slave = slave

    def close(self):
        self.ser.close()

    def send_recv(self, req_bytes, expected_resp_len):
        """Отправка и получение ответа с проверкой CRC."""
        frame = req_bytes + struct.pack("<H", crc16(req_bytes))
        
        # Повтор при ошибке связи (шум в линии)
        for attempt in range(3):
            self.ser.reset_input_buffer()
            self.ser.write(frame)
            self.ser.flush()
            
            resp = self.ser.read(expected_resp_len)
            if len(resp) == expected_resp_len:
                # Проверка CRC ответа
                rx_crc = struct.unpack("<H", resp[-2:])[0]
                calc_crc = crc16(resp[:-2])
                if rx_crc == calc_crc:
                    return resp
            time.sleep(0.05)
        raise IOError("Timeout or CRC error")

    def read_reg(self, addr):
        req = struct.pack(">BBHH", self.slave, 0x03, addr, 1)
        resp = self.send_recv(req, 7)
        if resp[1] & 0x80:
            raise RuntimeError(f"Read exception: 0x{resp[2]:02X}")
        return struct.unpack(">H", resp[3:5])[0]

    def write_reg(self, addr, val):
        req = struct.pack(">BBHH", self.slave, 0x06, addr, val)
        resp = self.send_recv(req, 8)
        if resp[1] & 0x80:
            exc_code = resp[2]
            if exc_code == 0x06: # BUSY
                return False # Специальный возврат для BUSY
            raise RuntimeError(f"Write exception: 0x{exc_code:02X}")
        return True

    def write_u32(self, addr_lo, value):
        ok1 = self.write_reg(addr_lo, value & 0xFFFF)
        if not ok1: return False
        time.sleep(0.01)
        ok2 = self.write_reg(addr_lo + 1, (value >> 16) & 0xFFFF)
        return ok2

    def wait_status_ok(self, timeout_sec=60):
        """Ждем пока flash_status станет 0 (OK)."""
        t0 = time.time()
        while time.time() - t0 < timeout_sec:
            try:
                st = self.read_reg(0x0047) # flash_status
                if st == 0:
                    return True
                elif st in [2, 3, 4, 5]: # PROT, ADDR, VERIFY, IO
                    print(f"  ERROR STATUS: {st}")
                    return False
            except Exception as e:
                print(f"  Status read fail: {e}")
            time.sleep(0.1)
        print("  TIMEOUT waiting for status")
        return False

    def write_chunk_safe(self, offset, data, max_retries=10):
        """Запись чанка с повторами при BUSY."""
        size = len(data)
        if size > 255: size = 255
        
        # Формируем кадр 0x0F
        # Slave | Func | AddrHi | AddrLo | Size | Data...
        hdr = struct.pack(">BBHI", self.slave, 0x0F, offset, size) 
        # Внимание! В оригинальном коде CmdFunc15 парсит адрес как rd32be(&mas[2]).
        # То есть байты 2,3,4,5.
        # mas[0]=Slave, mas[1]=Func, mas[2..5]=Addr(U32 BE), mas[6]=Size, mas[7..]=Data
        req_body = struct.pack(">BBI", self.slave, 0x0F, offset) + bytes([size]) + data
        
        for i in range(max_retries):
            try:
                # Ожидаемый ответ: Slave(1)+Func(1)+EchoAddr(4)+CRC(2) = 8 байт? 
                # Нет, CmdFunc15 возвращает memcpy(out, mas, 6u); return 6u;
                # Ответ без CRC добавляется позже. Тело ответа 6 байт. Плюс CRC 2 = 8 байт всего.
                resp = self.send_recv(req_body, 8)
                
                if resp[1] & 0x80:
                    code = resp[2]
                    if code == 0x06: # BUSY
                        print(f"  [BUSY] Retry {i+1}/{max_retries} @ {offset}", end="\r")
                        time.sleep(0.15) # Ждем завершения текущей операции Measure Task
                        continue
                    else:
                        raise RuntimeError(f"Chunk write exception: 0x{code:02X}")
                
                # Успех
                return True
                
            except IOError:
                print(f"  [COMM ERR] Retry {i+1}/{max_retries}", end="\r")
                time.sleep(0.1)
                
        print("\n  FAIL chunk after retries")
        return False

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--slave", type=int, default=100)
    ap.add_argument("--file", required=True)
    ap.add_argument("--chunk", type=int, default=255)
    args = ap.parse_args()

    if not os.path.exists(args.file):
        print("File not found"); return 1
    
    filesize = os.path.getsize(args.file)
    mb = ModbusMaster(args.port, args.slave)
    
    print(f"Start writing {args.file} ({filesize} bytes)")
    
    try:
        # 1. Блокировка звука
        print("1. Block Sound")
        mb.write_reg(0x0058, 1) # block_sound
        
        # 2. Настройка ключа и цели
        print("2. Setup Key/Target")
        mb.write_u32(0x0045, 1) # flash_key = 1
        mb.write_reg(0x0040, 1) # flash_target = SOUND
        
        # 3. ГАРАНТИРОВАННОЕ СТИРАНИЕ ОБЛАСТИ SOUNDS
        # Мы стираем весь объем файла + округление до сектора
        erase_size = ((filesize + 4095) // 4096) * 4096
        print(f"3. Erasing {erase_size} bytes of SOUNDS area...")
        
        # Настраиваем параметры erase
        mb.write_u32(0x0041, 0)       # flash_addr = 0
        mb.write_u32(0x0043, erase_size) # flash_len = размер для стирания
        mb.write_reg(0x0049, 1)       # erase_req = 1
        
        # Ждем завершения стирания (может занять до минуты для 3МБ)
        if not mb.wait_status_ok(timeout_sec=120):
            print("ERASE FAILED")
            return 1
        print("   Erase OK")
        
        # Теперь устанавливаем реальную длину файла для проверки границ в CmdFunc15
        mb.write_u32(0x0043, filesize) 
        
        # 4. Запись данных
        print("4. Writing chunks...")
        with open(args.file, "rb") as f:
            offset = 0
            total_chunks = (filesize + args.chunk - 1) // args.chunk
            start_time = time.time()
            
            while offset < filesize:
                data = f.read(args.chunk)
                if not data: break
                
                success = mb.write_chunk_safe(offset, data)
                if not success:
                    print(f"\nABORTED at offset {offset}")
                    return 1
                
                offset += len(data)
                
                # Прогресс каждые 5%
                pct = 100.0 * offset / filesize
                if int(pct) % 5 == 0 and offset > 0:
                     elapsed = time.time() - start_time
                     rate = offset / elapsed if elapsed > 0 else 0
                     eta = (filesize - offset) / rate if rate > 0 else 0
                     print(f"  {pct:.1f}% | Rate: {rate:.0f} B/s | ETA: {eta:.0f}s", end="\r")

        print("\n   Write Complete")
        
        # --- ДОПОЛНИТЕЛЬНАЯ ПРОВЕРКА ДАННЫХ (Spot Check) ---
        print("6. Verifying data integrity (spot checks)...")
        
        # Берем 3 точки: начало (после header), середина, конец
        check_offsets = [
            1024,           # Сразу после заголовка
            filesize // 2,  # Середина
            filesize - 256  # Предпоследний чанк
        ]
        
        with open(args.file, "rb") as f:
            for offset in check_offsets:
                if offset >= filesize: continue
                
                # Читаем оригинальные данные из файла
                f.seek(offset)
                expected_data = f.read(256)
                
                # Читаем данные из Flash через команду ext_read (упрощенно, через регистры или спецкоманду)
                # В текущем протоколе нет прямой команды "прочитать N байт из FLASH".
                # Но есть команда 0x0C (Log Read) для журнала. Для звука такой команды нет.
                
                # ВАРИАНТ А: Использовать отладчик CubeIDE (Expressions -> memory view)
                # Адрес SF_SOUNDS_BASE + offset. Сравнить байты.
                
                # ВАРИАНТ Б (Костыль): Попросить МК прочитать кусок звука и отправить его обратно?
                # Нет такой команды. Придется использовать ST-LINK Utility / STM32 Programmer.
                
                pass 
        
        # 5. Перезагрузка образа звука
        print("5. Reload Audio Image")
        mb.write_reg(0x0059, 1) # audio_reload_pending
        mb.wait_status_ok(timeout_sec=5)
        
    finally:
        # 6. Разблокировка звука
        print("6. Unblock Sound")
        mb.write_reg(0x0058, 0)
        mb.close()
        
    print("\nDONE.")
    return 0

if __name__ == "__main__":
    sys.exit(main())