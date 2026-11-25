#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_sound_test.py — проверка тестового воспроизведения (ALERT_SOUND_TEST).

Регистры (holding, 0x03/0x06):
  0x0060 req        W  1 = запуск (движок сбрасывает сам)
  0x0061 stop       W  1 = останов
  0x0062 index      RW индекс клипа в образе
  0x0063 repeats    RW повторов фазы (0/1 = один)
  0x0064 loop       RW 1 = крутить до stop/кнопки
  0x0065 active     R  тест идёт
  0x0066 source     R  0 = клип из flash, 1 = beep (образа нет)
  0x0067 last_index R  что играли
  0x0068/69 cnt_start R
  0x006A/6B cnt_done  R

Запуск:
  py tools/test_sound_test.py COM4 --slave 100
  py tools/test_sound_test.py COM4 --slave 100 --interactive   # + кнопка и гейт
  py tools/test_sound_test.py COM4 --slave 100 --monitor       # только наблюдение
  py tools/test_sound_test.py COM4 --slave 100 --all           # прогнать ВСЕ клипы образа
"""
import argparse, struct, sys, time

try:
    import serial
except ImportError:
    sys.exit("нужен pyserial:  py -m pip install pyserial")

R_REQ, R_STOP, R_INDEX, R_REPEATS, R_LOOP = 0x0060, 0x0061, 0x0062, 0x0063, 0x0064
R_ACTIVE, R_SOURCE, R_LAST = 0x0065, 0x0066, 0x0067
R_CNT_START, R_CNT_DONE, R_COUNT = 0x0068, 0x006A, 0x006C
R_BLOCK, R_POWER, R_FST = 0x0058, 0x0054, 0x0047

EXC = {0x01: "ILLEGAL_FUNCTION", 0x02: "ILLEGAL_ADDRESS",
       0x03: "ILLEGAL_VALUE", 0x04: "SLAVE_FAILURE", 0x06: "BUSY"}


def crc16(d):
    c = 0xFFFF
    for b in d:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return c


class MBExc(Exception):
    def __init__(self, code):
        self.code = code
        super().__init__("exception 0x%02X (%s)" % (code, EXC.get(code, "?")))


class MB:
    def __init__(self, port, slave, baud=115200, timeout=0.5):
        self.ser = serial.Serial(port, baud, timeout=timeout)
        self.slave = slave

    def close(self):
        self.ser.close()

    def _xfer(self, body):
        frame = body + struct.pack("<H", crc16(body))
        last = "no answer"
        for _ in range(3):
            self.ser.reset_input_buffer()
            self.ser.write(frame)
            self.ser.flush()
            hdr = self.ser.read(2)
            if len(hdr) < 2:
                last = "timeout"; time.sleep(0.05); continue
            if hdr[1] & 0x80:
                rest = self.ser.read(3)
                if len(rest) < 3:
                    last = "short exception"; continue
                resp = hdr + rest
                if crc16(resp[:-2]) != struct.unpack("<H", resp[-2:])[0]:
                    last = "CRC"; continue
                raise MBExc(resp[2])
            if hdr[1] == 0x03:
                bc = self.ser.read(1)
                if len(bc) < 1:
                    last = "short bytecount"; continue
                rest = self.ser.read(bc[0] + 2)
                if len(rest) < bc[0] + 2:
                    last = "short data"; continue
                resp = hdr + bc + rest
            else:
                rest = self.ser.read(6)
                if len(rest) < 6:
                    last = "short echo"; continue
                resp = hdr + rest
            if crc16(resp[:-2]) == struct.unpack("<H", resp[-2:])[0]:
                return resp
            last = "CRC"
            time.sleep(0.05)
        raise IOError(last)

    def rd(self, addr, cnt=1):
        r = self._xfer(struct.pack(">BBHH", self.slave, 0x03, addr, cnt))
        return [struct.unpack(">H", r[3 + i * 2:5 + i * 2])[0] for i in range(cnt)]

    def wr(self, addr, val):
        self._xfer(struct.pack(">BBHH", self.slave, 0x06, addr, val & 0xFFFF))

    def state(self):
        w = self.rd(R_REQ, 13)
        return dict(req=w[0], stop=w[1], index=w[2], repeats=w[3], loop=w[4],
                    active=w[5], source=w[6], last=w[7],
                    start=w[8] | (w[9] << 16), done=w[10] | (w[11] << 16),
                    count=w[12])


RESULTS = []


def check(name, ok, info=""):
    RESULTS.append((name, ok))
    print("  [%s] %s%s" % ("PASS" if ok else "FAIL", name, ("  " + info) if info else ""))
    return ok


def wait_active(mb, want, timeout=3.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        s = mb.state()
        if s["active"] == want:
            return s, time.time() - t0
        time.sleep(0.05)
    return mb.state(), -1.0


def start(mb, index, repeats=1, loop=0):
    mb.wr(R_INDEX, index)
    mb.wr(R_REPEATS, repeats)
    mb.wr(R_LOOP, loop)
    mb.wr(R_REQ, 1)


def one_shot(mb, index, tag, timeout=20.0):
    before = mb.state()
    start(mb, index, 1, 0)
    s, t_on = wait_active(mb, 1, 3.0)
    if not check(tag + ": взведён", s["active"] == 1,
                 "за %.2f с, source=%d last=%d" % (t_on, s["source"], s["last"])):
        return None
    src = s["source"]
    s, t_off = wait_active(mb, 0, timeout)
    check(tag + ": завершён сам", s["active"] == 0, "проиграл %.2f с" % t_off)
    check(tag + ": cnt_start +1", s["start"] == before["start"] + 1,
          "%d -> %d" % (before["start"], s["start"]))
    check(tag + ": cnt_done +1", s["done"] == before["done"] + 1,
          "%d -> %d" % (before["done"], s["done"]))
    return src


def sweep_all(mb, timeout_per=30.0):
    """Прогнать ВСЕ клипы образа по одному: каждый обязан взвестись и
    завершиться сам (cnt_done +1). Клип длиннее timeout_per -> FAIL, но
    перебор продолжается дальше (next start сам погасит зависший через
    Alert_Stop() внутри SoundTest_Tick)."""
    n = mb.state()["count"]
    print("S. полный перебор образа: %d звуков" % n)
    if n == 0:
        check("S: образ непустой", False, "count=0 (образа нет или он пуст)")
        return
    ok = 0
    for i in range(n):
        before = mb.state()
        start(mb, i, 1, 0)
        s, t_on = wait_active(mb, 1, 3.0)
        if s["active"] != 1:
            check("S[%d]: взведён" % i, False, "не взводится за 3 с")
            continue
        src = s["source"]
        s, t_off = wait_active(mb, 0, timeout_per)
        good = (s["active"] == 0) and (s["done"] == before["done"] + 1)
        note = "%.2f с, source=%d" % (t_off, src)
        if t_off < 0:
            note += " (ТАЙМАУТ %g с - клип длиннее или завис)" % timeout_per
        check("S[%d]: проигран" % i, good, note)
        if good:
            ok += 1
    print("   перебор: %d/%d клипов отыграли полностью" % (ok, n))
    check("S: все клипы отыграли", ok == n, "%d/%d" % (ok, n))


def monitor(mb):
    print("Мониторинг (Ctrl+C для выхода). Жми кнопки / пиши регистры из конфигуратора.")
    prev = None
    try:
        while True:
            s = mb.state()
            cur = (s["active"], s["source"], s["last"], s["start"], s["done"])
            if cur != prev:
                print("  active=%d source=%d last=%d start=%d done=%d" % cur)
                prev = cur
            time.sleep(0.2)
    except KeyboardInterrupt:
        print()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--slave", type=int, default=100)
    ap.add_argument("--clip", type=int, default=1, help="какой клип проверить (по умолчанию 1)")
    ap.add_argument("--loop-hold", type=float, default=3.0)
    ap.add_argument("--interactive", action="store_true",
                    help="добавить тесты с кнопкой и гейтом block_sound")
    ap.add_argument("--monitor", action="store_true")
    ap.add_argument("--all", action="store_true",
                    help="перебрать весь образ: прогнать каждый клип по одному")
    ap.add_argument("--sweep-timeout", type=float, default=30.0,
                    help="секунд на один клип при --all (по умолчанию 30)")
    a = ap.parse_args()

    mb = MB(a.port, a.slave)
    print("=" * 68)
    print("  SOUND TEST  port=%s slave=%d" % (a.port, a.slave))
    print("=" * 68)

    try:
        if a.monitor:
            monitor(mb)
            return 0

        # --- 0. предусловия -------------------------------------------------
        try:
            power = mb.rd(R_POWER)[0]
            blk = mb.rd(R_BLOCK)[0]
            fst = mb.rd(R_FST)[0]
        except (IOError, MBExc) as e:
            print("Нет связи: %s" % e)
            return 1
        print("power=%d (0=ON,1=OFF,2=PRE_INIT,3=ON_1)  block_sound=%d  flash_status=%d"
              % (power, blk, fst))
        if power in (1, 2):
            print("Прибор выключен/на пред-ините — Alert_Tick не работает. Включи и повтори.")
            return 1
        if blk != 0:
            print("Снимаю block_sound (иначе плеер дропает клипы)")
            mb.wr(R_BLOCK, 0)
            time.sleep(0.3)

        s0 = mb.state()
        print("baseline: start=%d done=%d active=%d" % (s0["start"], s0["done"], s0["active"]))
        if s0["active"]:
            mb.wr(R_STOP, 1)
            wait_active(mb, 0, 3.0)
        print()

        # --- A. одиночный клип #0 ------------------------------------------
        print("A. одиночный прогон клипа #0")
        src = one_shot(mb, 0, "A")
        if src == 1:
            print("   source=1 -> образа во flash нет, играл аварийный писк (ожидаемо)")
        print()

        # --- B. выбранный клип ---------------------------------------------
        print("B. одиночный прогон клипа #%d" % a.clip)
        one_shot(mb, a.clip, "B")
        s = mb.state()
        if src == 0:
            check("B: last_index == %d" % a.clip, s["last"] == a.clip, "last=%d" % s["last"])
        print()

        # --- C. индекс вне диапазона -> клампинг ---------------------------
        print("C. индекс 0xFFFF (вне диапазона)")
        one_shot(mb, 0xFFFF, "C")
        s = mb.state()
        if src == 0:
            check("C: индекс сведён к 0", s["last"] == 0, "last=%d" % s["last"])
        else:
            print("   образа нет — клампинг не применяется, source=1")
        print()

        # --- D. цикл + стоп -------------------------------------------------
        print("D. цикл %s с, затем stop" % a.loop_hold)
        before = mb.state()
        start(mb, 0, 0, 1)
        s, _ = wait_active(mb, 1, 3.0)
        check("D: взведён", s["active"] == 1)
        time.sleep(a.loop_hold)
        s = mb.state()
        check("D: всё ещё идёт (не завершается сам)", s["active"] == 1)
        mb.wr(R_STOP, 1)
        s, t = wait_active(mb, 0, 3.0)
        check("D: остановлен по stop", s["active"] == 0, "за %.2f с" % t)
        check("D: cnt_done +1", s["done"] == before["done"] + 1)
        print()

        # --- E. перезапуск на ходу -----------------------------------------
        print("E. перезапуск с новым индексом во время цикла")
        start(mb, 0, 0, 1)
        wait_active(mb, 1, 3.0)
        time.sleep(0.5)
        start(mb, a.clip, 0, 1)
        time.sleep(1.0)
        s = mb.state()
        check("E: остался активен", s["active"] == 1)
        if src == 0:
            check("E: last_index switched", s["last"] == a.clip, "last=%d" % s["last"])
        mb.wr(R_STOP, 1)
        wait_active(mb, 0, 3.0)
        print()

        # --- S. полный перебор образа --------------------------------------
        if a.all:
            sweep_all(mb, a.sweep_timeout)
            print()

        # --- F/G. интерактив ------------------------------------------------
        if a.interactive:
            print("F. гейт block_sound: клип НЕ должен звучать")
            mb.wr(R_BLOCK, 1)
            time.sleep(0.3)
            input("   Нажми Enter — запущу клип #0. Слушай: должно быть ТИХО... ")
            before = mb.state()
            start(mb, 0, 1, 0)
            wait_active(mb, 1, 3.0)
            s, t = wait_active(mb, 0, 10.0)
            print("   active снялся за %.2f с, cnt_start=%d" % (t, s["start"]))
            ans = input("   Тишина была? (y/n): ").strip().lower()
            check("F: block_sound глушит тест", ans == "y")
            mb.wr(R_BLOCK, 0)
            time.sleep(0.3)
            print()

            print("G. квитирование кнопкой во время цикла")
            start(mb, 0, 0, 1)
            wait_active(mb, 1, 3.0)
            input("   Звук идёт по кругу. Нажми ЛЮБУЮ кнопку, потом Enter... ")
            s, t = wait_active(mb, 0, 3.0)
            check("G: кнопка закрыла тест целиком", s["active"] == 0, "за %.2f с" % t)
            if s["active"]:
                mb.wr(R_STOP, 1)
                wait_active(mb, 0, 3.0)
            print()

        # --- итог -----------------------------------------------------------
        mb.wr(R_STOP, 1)
        mb.wr(R_BLOCK, 0)
        s = mb.state()
        print("=" * 68)
        ok = sum(1 for _, v in RESULTS if v)
        print("  ИТОГ: %d/%d PASS   start=%d done=%d active=%d"
              % (ok, len(RESULTS), s["start"], s["done"], s["active"]))
        for n, v in RESULTS:
            if not v:
                print("   FAIL: " + n)
        print("=" * 68)
        return 0 if ok == len(RESULTS) else 1

    except MBExc as e:
        print("\nModbus %s — проверь, что патч применён и регистры 0x0060+ в карте" % e)
        return 1
    except IOError as e:
        print("\nСвязь: %s" % e)
        return 1
    finally:
        mb.close()


if __name__ == "__main__":
    sys.exit(main())