#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_flash_write_final.py - Запись звука с замером времени Erase/Write.
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
        frame = req_bytes + struct.pack("<H", crc16(req_bytes))
        
        # Повтор при ошибке связи (шум в линии)
        for attempt in range(3):
            self.ser.reset_input_buffer()
            self.ser.write(frame)
            self.ser.flush()
            
            resp = self.ser.read(expected_resp_len)
            if len(resp) == expected_resp_len:
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
        """Ждем пока flash_status станет 0 (OK). Возвращает время ожидания в сек."""
        t_start = time.time()
        while time.time() - t_start < timeout_sec:
            try:
                st = self.read_reg(0x0047) # flash_status
                if st == 0:
                    return time.time() - t_start # Успех, вернули время
                elif st in [2, 3, 4, 5]: # PROT, ADDR, VERIFY, IO
                    print(f"  ERROR STATUS: {st}")
                    return -1
            except Exception as e:
                print(f"  Status read fail: {e}")
            time.sleep(0.1)
        print("  TIMEOUT waiting for status")
        return -1

    def write_chunk_safe(self, offset, data, max_retries=10):
        """Запись чанка с повторами при BUSY."""
        size = len(data)
        if size > 255: size = 255
        
        req_body = struct.pack(">BBI", self.slave, 0x0F, offset) + bytes([size]) + data
        
        for i in range(max_retries):
            try:
                resp = self.send_recv(req_body, 8)
                
                if resp[1] & 0x80:
                    code = resp[2]
                    if code == 0x06: # BUSY
                        time.sleep(0.15)
                        continue
                    else:
                        raise RuntimeError(f"Chunk write exception: 0x{code:02X}")
                
                return True
                
            except IOError:
                time.sleep(0.1)
                
        return False

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--slave", type=int, default=0)
    ap.add_argument("--file", required=True)
    ap.add_argument("--chunk", type=int, default=255)
    args = ap.parse_args()

    if not os.path.exists(args.file):
        print("File not found"); return 1
    
    filesize = os.path.getsize(args.file)
    mb = ModbusMaster(args.port, args.slave)
    
    print(f"Start writing {args.file} ({filesize} bytes)")
    total_start_time = time.time()
    
    try:
        # 1. Блокировка звука
        print("\n1. Block Sound")
        mb.write_reg(0x0058, 1) # block_sound
        
        # 2. Настройка ключа и цели
        print("2. Setup Key/Target")
        mb.write_u32(0x0045, 1) # flash_key = 1
        mb.write_reg(0x0040, 1) # flash_target = SOUND
        
        # 3. ГАРАНТИРОВАННОЕ СТИРАНИЕ ОБЛАСТИ SOUNDS
        erase_size = ((filesize + 4095) // 4096) * 4096
        sectors_count = erase_size // 4096
        print(f"\n3. Erasing {erase_size} bytes ({sectors_count} sectors)...")
        
        mb.write_u32(0x0041, 0)       # flash_addr = 0
        mb.write_u32(0x0043, erase_size) # flash_len = размер для стирания
        mb.write_reg(0x0049, 1)       # erase_req = 1
        
        # КРИТИЧНО: Ждем, пока Measure Task увидит флаг и сменит статус на BUSY
        # Иначе мы прочитаем старый OK=0 до начала работы
        print("   Waiting for task to pick up request...")
        t_wait = time.time()
        while time.time() - t_wait < 2.0: # Ждем максимум 1 секунду
            st = mb.read_reg(0x0047)
            if st == 1: # CV_ST_BUSY
                break
            time.sleep(0.05)
        
        # Теперь начинаем замер реального времени стирания
        erase_time = mb.wait_status_ok(timeout_sec=120)
        
        if erase_time < 0:
            print("ERASE FAILED")
            return 1
            
        avg_erase_ms = (erase_time * 1000) / sectors_count
        print(f"   Erase OK. Real Time: {erase_time:.2f}s | Avg per sector: {avg_erase_ms:.1f}ms")
        
        # Теперь устанавливаем реальную длину файла для проверки границ в CmdFunc15
        mb.write_u32(0x0043, filesize) 
        
        # 4. Запись данных
        print(f"\n4. Writing chunks (chunk={args.chunk} bytes)...")
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
                
                # Прогресс каждые 5% или каждый 100-й чанк
                pct = 100.0 * offset / filesize
                current_chunk_num = offset // args.chunk
                if (int(pct) % 5 == 0 and offset > 0) or (current_chunk_num % 100 == 0):
                     elapsed = time.time() - start_time
                     rate = offset / elapsed if elapsed > 0 else 0
                     eta = (filesize - offset) / rate if rate > 0 else 0
                     print(f"  {pct:.1f}% | Chunks: {current_chunk_num}/{total_chunks} | Rate: {rate:.0f} B/s | ETA: {eta:.0f}s", end="\r")

        write_time = time.time() - start_time
        avg_write_ms = (write_time * 1000) / total_chunks
        print(f"\n   Write Complete. Time: {write_time:.2f}s | Avg per chunk: {avg_write_ms:.1f}ms")
        
        # 5. Перезагрузка образа звука
        print("\n5. Reload Audio Image")
        reload_start = time.time()
        mb.write_reg(0x0059, 1) # audio_reload_pending
        mb.wait_status_ok(timeout_sec=5)
        reload_time = time.time() - reload_start
        print(f"   Reload done. Time: {reload_time:.2f}s")
        
    finally:
        # 6. Разблокировка звука
        print("\n6. Unblock Sound")
        mb.write_reg(0x0058, 0)
        mb.close()
        
    total_time = time.time() - total_start_time
    print(f"\n{'='*40}")
    print(f"DONE. Total operation time: {total_time:.2f}s")
    print(f"Erase: {erase_time:.2f}s | Write: {write_time:.2f}s | Reload: {reload_time:.2f}s")
    print(f"Avg throughput (write phase): {(filesize/write_time)/1024:.1f} KB/s")
    print('='*40)
    return 0

if __name__=="__main__":
    sys.exit(main())