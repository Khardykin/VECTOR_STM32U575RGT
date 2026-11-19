#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
upload_flash.py - загрузка данных во внешнюю SPI flash прибора по COM (USART3).

Кадр: [slave][cmd][addr32 BE][size16 BE][payload][CRC16 Modbus lo,hi]
Ответ: тот же формат; ошибка: [slave][cmd|0x80][code][CRC].
Команды: 0x0A COMMIT, 0x0B INFO, 0x0D ERASE, 0x0E READ, 0x0F WRITE.

  py tools/upload_flash.py -p COM7 tools/sounds.bin
  py tools/upload_flash.py -p COM7 --info
  py tools/upload_flash.py -p COM7 --addr 0x400000 cfg.bin --force
"""
import argparse, struct, sys, time

CMD_WRITE = 0x0F                 # CmdFunc15: блочная запись во flash (Avis)
REG_COMMIT      = 0x0F00
REG_SAVE_CFG    = 0x0F01
REG_FLASH_LO    = 0x0F10
REG_IMG_OK      = 0x0F1B
REG_IMG_REFRESH = 0x0F1C
REG_ERASE_BASE  = 0x0F20
REG_ERASE_BUSY  = 0x0F26
REG_ERASE_ERR   = 0x0F27
REG_ERASE_PCT   = 0x0F28
REG_ERASE_ADDR  = 0x0F29
REG_ERASE_LEN   = 0x0F2B
REG_ERASE_KEY   = 0x0F2D
REG_ERASE_START = 0x0F2E
REG_WIN_ADDR    = 0x0F30
REG_WIN_LEN     = 0x0F32
REG_WIN_CMD     = 0x0F33
REG_WIN_STATUS  = 0x0F34
REG_WIN_AUTOINC = 0x0F35
REG_WIN_DATA    = 0x0F40
MAX_PAYLOAD = 1024
VERIFY_READ = 250          # ответ READ ограничен таймаутом HAL_UART_Transmit(100 мс)
ERASE_STEP = 16 * 1024     # 4 сектора за команду - не держим receiver_task надолго
SOUNDS_LIMIT = 0x00400000
ERASE_KEY = 0xA5
ERR_TEXT = {1: "битый кадр / неверная длина", 2: "адрес вне микросхемы",
            3: "ошибка обращения к flash", 4: "сверка не совпала",
            5: "область защищена (нужен --force)", 6: "неизвестная команда"}


class FlashError(Exception):
    pass


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc & 0xFFFF


def build_frame(slave, cmd, addr, payload=b""):
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload > %d" % MAX_PAYLOAD)
    body = struct.pack(">BBIH", slave, cmd, addr & 0xFFFFFFFF, len(payload)) + payload
    return body + struct.pack("<H", crc16(body))


class FlashClient:
    def __init__(self, transport, slave=1, timeout=3.0, retries=3, log=None):
        self.t, self.slave, self.timeout, self.retries = transport, slave, timeout, retries
        self.log = log or (lambda s: None)

    def _read_exact(self, n, timeout):
        buf, deadline = b"", time.time() + timeout
        while len(buf) < n:
            chunk = self.t.read(n - len(buf), max(0.01, deadline - time.time()))
            if chunk:
                buf += chunk
            elif time.time() >= deadline:
                raise TimeoutError("таймаут: %d из %d байт" % (len(buf), n))
        return buf

    def _xfer(self, cmd, addr, payload=b"", timeout=None):
        frame, to = build_frame(self.slave, cmd, addr, payload), timeout or self.timeout
        last = None
        for attempt in range(self.retries):
            try:
                self.t.reset()
                self.t.write(frame)
                head = self._read_exact(2, to)
                if head[1] & 0x80:
                    rest = self._read_exact(3, to)
                    if crc16(head + rest[:1]) != struct.unpack("<H", rest[1:])[0]:
                        raise FlashError("CRC ответа (ошибка)")
                    raise FlashError("устройство: %s (код %d)" %
                                     (ERR_TEXT.get(rest[0], "неизвестно"), rest[0]))
                if head[0] != self.slave or head[1] != cmd:
                    raise FlashError("не тот адрес/команда в ответе")
                rest = self._read_exact(6, to)
                raddr, size = struct.unpack(">IH", rest)
                body = self._read_exact(size + 2, to)
                data, rcrc = body[:size], struct.unpack("<H", body[size:])[0]
                if crc16(head + rest + data) != rcrc:
                    raise FlashError("CRC ответа")
                if raddr != (addr & 0xFFFFFFFF):
                    raise FlashError("адрес в ответе не совпал")
                return data
            except FlashError:
                raise
            except Exception as e:
                last = e
                self.log("  повтор %d/%d: %s" % (attempt + 1, self.retries, e))
                time.sleep(0.05)
        raise FlashError("нет ответа после %d попыток: %s" % (self.retries, last))

    # --- Modbus 0x03 / 0x10 -----------------------------------------------
    def _mb(self, func, addr, count, data=b"", timeout=None):
        body = struct.pack(">BBHH", self.slave, func, addr, count)
        if func == 0x10:
            body += struct.pack("B", len(data)) + data
        frame = body + struct.pack("<H", crc16(body))
        to = timeout or self.timeout
        last = None
        for attempt in range(self.retries):
            try:
                self.t.reset()
                self.t.write(frame)
                head = self._read_exact(2, to)
                if head[1] & 0x80:
                    rest = self._read_exact(3, to)
                    raise FlashError("устройство: %s (код %d)" %
                                     (ERR_TEXT.get(rest[0], "неизвестно"), rest[0]))
                if func == 0x10:
                    rest = self._read_exact(6, to)
                    if crc16(head + rest[:4]) != struct.unpack("<H", rest[4:])[0]:
                        raise FlashError("CRC ответа")
                    return []
                bc = self._read_exact(1, to)[0]
                body_r = self._read_exact(bc + 2, to)
                if crc16(head + bytes([bc]) + body_r[:bc]) != struct.unpack("<H", body_r[bc:])[0]:
                    raise FlashError("CRC ответа")
                return [struct.unpack(">H", body_r[i*2:i*2+2])[0] for i in range(bc // 2)]
            except FlashError:
                raise
            except Exception as e:
                last = e
                self.log("  повтор %d/%d: %s" % (attempt + 1, self.retries, e))
                time.sleep(0.05)
        raise FlashError("нет ответа после %d попыток: %s" % (self.retries, last))

    def read_regs(self, addr, count):
        return self._mb(0x03, addr, count)

    def write_regs(self, addr, values):
        data = b"".join(struct.pack(">H", v & 0xFFFF) for v in values)
        self._mb(0x10, addr, len(values), data)

    def info(self):
        r = self.read_regs(REG_FLASH_LO, 12)      # 0x0F10..0x0F1B
        total = (r[1] << 16) | r[0]
        magic = (r[5] << 16) | r[4]
        img   = (r[9] << 16) | r[8]
        return dict(flash_size=total, sector=r[2], page=r[3],
                    magic=struct.pack("<I", magic), image_ok=bool(r[11]),
                    version=r[6], sounds=r[7], image_size=img, rate=r[10])

    def commit(self):
        self.write_regs(REG_COMMIT, [1])

    def erase(self, addr, length, force=False, progress=None):
        # Диапазон стирается ФОНОМ в приборе, мы опрашиваем BUSY/PROGRESS.
        self.write_regs(REG_ERASE_ADDR, [addr & 0xFFFF, (addr >> 16) & 0xFFFF])
        self.write_regs(REG_ERASE_LEN,  [length & 0xFFFF, (length >> 16) & 0xFFFF])
        self.write_regs(REG_ERASE_KEY,  [ERASE_KEY if force else 0])
        self.write_regs(REG_ERASE_START, [1])
        deadline = time.time() + 300.0
        while True:
            st = self.read_regs(REG_ERASE_BUSY, 3)   # BUSY, ERR, PCT
            if st[1] & 0x40:
                raise FlashError("стирание отклонено (адрес/длина или нужен --force)")
            if progress:
                progress(length * st[2] // 100, length)
            if st[0] == 0:
                break
            if time.time() > deadline:
                raise FlashError("таймаут стирания")
            time.sleep(0.2)
        if progress:
            progress(length, length)

    def write(self, addr, data):
        self._xfer(CMD_WRITE, addr, data)

    def read(self, addr, n):
        if not 1 <= n <= 250:
            raise ValueError("read: 1..250")
        even = n + (n & 1)
        self.write_regs(REG_WIN_ADDR, [addr & 0xFFFF, (addr >> 16) & 0xFFFF])
        self.write_regs(REG_WIN_LEN,  [even])
        self.write_regs(REG_WIN_CMD,  [1])
        st = self.read_regs(REG_WIN_STATUS, 1)[0]
        if st:
            raise FlashError("чтение окна: %s (код %d)" % (ERR_TEXT.get(st, "?"), st))
        regs = self.read_regs(REG_WIN_DATA, even // 2)
        return b"".join(struct.pack(">H", v) for v in regs)[:n]

    def upload(self, path, addr=0, chunk=1024, do_erase=True, do_verify=True,
               force=False, progress=None):
        blob = open(path, "rb").read()
        size = len(blob)
        if size == 0:
            raise FlashError("пустой файл")
        if addr + size > self.info()["flash_size"]:
            raise FlashError("файл не влезает в микросхему")
        if not force and addr + size > SOUNDS_LIMIT:
            raise FlashError("адрес выше 0x%X (CONFIG/LOG) - нужен --force" % SOUNDS_LIMIT)
        if do_erase:
            self.erase(addr, size, force=force,
                       progress=lambda d, t: progress("erase", d, t) if progress else None)
        sent, t0 = 0, time.time()
        while sent < size:
            part = blob[sent:sent + chunk]
            self.write(addr + sent, part)
            sent += len(part)
            if progress:
                progress("write", sent, size)
        if do_verify:
            done = 0
            while done < size:
                n = min(VERIFY_READ, size - done)
                if self.read(addr + done, n) != blob[done:done + n]:
                    raise FlashError("сверка не совпала на 0x%06X" % (addr + done))
                done += n
                if progress:
                    progress("verify", done, size)
        self.commit()
        return size, time.time() - t0


class SerialTransport:
    def __init__(self, port, baud):
        import serial
        self.s = serial.Serial(port, baud, timeout=0.05, write_timeout=2.0)

    def reset(self):
        self.s.reset_input_buffer()

    def write(self, data):
        self.s.write(data); self.s.flush()

    def read(self, n, timeout):
        self.s.timeout = min(0.1, max(0.01, timeout))
        return self.s.read(n)

    def close(self):
        self.s.close()


def _bar(stage, done, total, t0):
    pct = 100.0 * done / total if total else 100.0
    el = time.time() - t0
    sp = done / el / 1024 if el > 0 else 0
    sys.stdout.write("\r%-6s %3.0f%%  %8d/%d Б  %6.1f КБ/с" % (stage, pct, done, total, sp))
    sys.stdout.flush()
    if done >= total:
        sys.stdout.write("\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file", nargs="?")
    ap.add_argument("-p", "--port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--slave", type=int, default=1)
    ap.add_argument("--addr", type=lambda x: int(x, 0), default=0)
    ap.add_argument("--chunk", type=int, default=1024)
    ap.add_argument("--no-erase", action="store_true")
    ap.add_argument("--no-verify", action="store_true")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--info", action="store_true")
    ap.add_argument("--read", type=int, metavar="N")
    ap.add_argument("--out")
    ap.add_argument("--erase-only", type=lambda x: int(x, 0), metavar="LEN")
    a = ap.parse_args()
    if not a.port:
        ap.error("нужен --port (например -p COM7)")
    if a.chunk > MAX_PAYLOAD:
        ap.error("--chunk максимум %d" % MAX_PAYLOAD)
    if a.read and a.read > VERIFY_READ:
        ap.error("--read максимум %d (таймаут передачи COM)" % VERIFY_READ)
    tr = SerialTransport(a.port, a.baud)
    cli = FlashClient(tr, slave=a.slave, log=print)
    try:
        i = cli.info()
        print("flash %d КБ, сектор %d, страница %d" % (i["flash_size"] // 1024, i["sector"], i["page"]))
        print("образ: magic=%s ok=%s звуков=%d частота=%d размер=%d" %
              (i["magic"], i["image_ok"], i["sounds"], i["rate"], i["image_size"]))
        if a.info:
            return 0
        if a.read:
            d = cli.read(a.addr, a.read)
            if a.out:
                open(a.out, "wb").write(d); print("прочитано %d Б -> %s" % (len(d), a.out))
            else:
                print(d.hex(" "))
            return 0
        if a.erase_only:
            t0 = time.time()
            cli.erase(a.addr, a.erase_only, force=a.force, progress=lambda d, t: _bar("erase", d, t, t0))
            print("стёрто %d Б от 0x%06X" % (a.erase_only, a.addr)); return 0
        if not a.file:
            ap.error("нужен файл (или --info / --read / --erase-only)")
        t0 = time.time()
        size, el = cli.upload(a.file, addr=a.addr, chunk=a.chunk,
                              do_erase=not a.no_erase, do_verify=not a.no_verify, force=a.force,
                              progress=lambda st, d, t: _bar(st, d, t, t0))
        print("готово: %d Б за %.1f с (%.1f КБ/с), адрес 0x%06X" % (size, el, size / el / 1024, a.addr))
        i = cli.info()
        print("образ после записи: ok=%s звуков=%d частота=%d" % (i["image_ok"], i["sounds"], i["rate"]))
        return 0
    except FlashError as e:
        print("\n[!] %s" % e); return 1
    finally:
        try:
            cli.commit()      # закрывает сессию: эхо и лог возвращаются
        except Exception:
            pass
        tr.close()


if __name__ == "__main__":
    sys.exit(main())
