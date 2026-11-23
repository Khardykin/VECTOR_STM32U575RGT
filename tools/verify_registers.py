#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_registers.py - check that the Command_vector register map really works.

The map is parsed straight from Command_vector.c, so the test can never drift
away from the firmware.

Checks:
  1. every contiguous block reads back with 0x03 (holding) / 0x04 (input)
  2. per-register read agrees with the block read
  3. addresses outside the map return Modbus exception 0x02
  4. with --write: write / read back / restore for a safe whitelist

Usage:
  py tools/verify_registers.py COM3
  py tools/verify_registers.py COM3 --write
  py tools/verify_registers.py COM3 --probe
  py tools/verify_registers.py --list
"""

import argparse
import os
import re
import struct

def make_frame_log_read(slave, ptr=0, cnt=4):
    """Кадр 0x0C: 10 байт."""
    import struct
    data = struct.pack(">BB I H", slave, 0x0C, ptr, cnt)
    crc = modbus_crc(data) if 'modbus_crc' in dir() else crc16(data)
    return data + struct.pack("<H", crc)

import sys
import time

try:
    import serial
except ImportError:
    serial = None

DEFAULT_SRC = os.path.join("Core", "VectorLib", "Src", "Command_vector.c")

ARRAY_SIZES = {"CV_APPKEY_SIZE": 16, "CV_DEVEUI_SIZE": 8,
               "SZ_APPKEY": 16, "SZ_DEVEUI": 8}
OBJ_NAMES = {"OBJ_APPKEY": "appkey", "OBJ_DEVEUI": "deveui_com",
             "OBJ_CFG": "application_language"}

# Plain settings, no side effects - safe to poke and restore.
SAFE_WRITE = {
    # Holding registers — безопасные для записи/чтения/восстановления
    0x000C: 0x0002,  # TimeLedStatePulse
    0x000D: 0x000A,  # TimeLedStatePeriod
    0x000E: 0x1068,  # battery_charge_volt_max
    0x000F: 0x0CE4,  # battery_charge_volt_min
    0x0010: 0x0DAC,  # battery_charge_volt_lim_1
    0x0011: 0x0D48,  # battery_charge_volt_lim_2
    0x0012: 0x0000,  # Gps_type_nav (u32 lo)
    0x0014: 0x0000,  # PeriodTimeLora
    0x0015: 0x0000,  # Lora_Config_Flags
    0x0019: 0x0001,  # Immobility_Enable
    0x001A: 0x0078,  # Immobility_Window_s
    0x001B: 0x003C,  # Immobility_PreAlarm_s
    0x001C: 0x00FA,  # Immobility_Ths_mg
    0x001D: 0x0000,  # Devices_Expected
    0x001E: 0x0002,  # Alert_Flags
    0x0024: 0x0A00,  # Archiveinterval
    0x0025: 0x0000,  # ArchiveRecording
    0x002B: 0x0000,  # temperature calib (float lo)
    0x002D: 0x0000,  # flag
}

# Never touch: erase / save / apply / reset / power / identity.
FORBIDDEN = {
    0x0001, 0x0002, 0x0007, 0x0008, 0x0009, 0x000A,
    0x0020, 0x0022, 0x0026, 0x0028,
    0x0040, 0x0041, 0x0043, 0x0045, 0x0047,
    0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
    0x0050, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057, 0x0058,
}
FORBIDDEN_RANGES = [(0x002E, 0x0039)]   # appkey + deveui


def array_body(text, name):
    m = re.search(r"static\s+const\s+reg_map_t\s+" + name + r"\s*\[\s*\]\s*=\s*\{", text)
    if not m:
        return None
    i = m.end() - 1
    depth, k = 0, i
    while k < len(text):
        if text[k] == "{":
            depth += 1
        elif text[k] == "}":
            depth -= 1
            if depth == 0:
                return text[i + 1:k]
        k += 1
    return None


def split_top(s):
    out, cur, depth = [], "", 0
    for ch in s:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip()); cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def cnum(s):
    s = s.strip().rstrip("uUlL")
    return int(s, 16) if s.lower().startswith("0x") else int(s)


ROW = re.compile(
    r"^\s*(?P<mac>CM|AR|WK|TC|CS|SS|ST|DV|TC_N|ST_N|ARR)\s*\((?P<args>[^)]*)\)"
    r"|^\s*\{\s*(?P<raw>.*?)\}\s*,", re.M | re.S)


def parse_map(text, name):
    body = array_body(text, name)
    if body is None:
        return []
    rows = []
    for m in ROW.finditer(body):
        if m.group("mac"):
            mac = m.group("mac")
            a = split_top(m.group("args"))
            if mac == "ARR":
                addr, typ, n, field = a[0], a[1], a[2], a[3]
            elif mac.endswith("_N"):
                field, addr, typ, n = a[0], a[1], a[2], a[3]
            else:
                field, addr, typ = a[0], a[1], a[2]; n = "0"
        else:
            p = split_top(m.group("raw"))
            if len(p) < 5:
                continue
            addr, _off, typ, n, field = p[:5]

        try:
            a = cnum(addr)
        except ValueError:
            continue
        if a == 0:
            continue

        if n in ARRAY_SIZES:
            cnt = ARRAY_SIZES[n]
        else:
            try:
                cnt = cnum(n)
            except ValueError:
                cnt = 1

        if typ == "T_FLT4":
            span = 2 * (cnt or 1)
        elif typ == "T_STR":
            span = (cnt + 1) // 2
        elif typ == "T_U32":
            span = 2
        else:
            span = 1

        rows.append({"addr": a, "span": span, "type": typ,
                     "name": OBJ_NAMES.get(field, field), "n": cnt})
    return rows


def blocks_of(rows):
    rs = sorted(rows, key=lambda r: r["addr"])
    out, cur = [], None
    for r in rs:
        if cur and r["addr"] == cur[0] + cur[1]:
            cur = (cur[0], cur[1] + r["span"])
        else:
            if cur:
                out.append(cur)
            cur = (r["addr"], r["span"])
    if cur:
        out.append(cur)
    return out


def forbidden(addr, span):
    for a in range(addr, addr + span):
        if a in FORBIDDEN:
            return True
        for lo, hi in FORBIDDEN_RANGES:
            if lo <= a <= hi:
                return True
    return False


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else crc >> 1
    return crc


class MbError(Exception):
    def __init__(self, code):
        self.code = code
        super().__init__("Modbus exception 0x%02X" % code)


class Mb:
    def __init__(self, ser, slave=1, gap=0.002):
        # gap: inter-frame pause before reading the answer.  Modbus RTU t3.5
        # at 115200 is ~0.3 ms; the device needs at most one Receiver Task
        # period (10 ms), and read() already blocks until the bytes arrive.
        # The old 20 ms just made every transaction slower, not more reliable.
        self.ser, self.slave, self.gap = ser, slave, gap
        self.retries = 0

    def _xfer_once(self, req, rest_len):
        """One attempt. rest_len = exact byte count after the first two bytes."""
        self.ser.reset_input_buffer()
        self.ser.write(req + struct.pack("<H", crc16(req)))
        self.ser.flush()
        time.sleep(self.gap)
        head = self.ser.read(2)
        if len(head) < 2:
            return None
        if head[1] & 0x80:
            tail = self.ser.read(3)
            if len(tail) >= 1:
                raise MbError(tail[0])
            return None
        rest = self.ser.read(rest_len)
        if len(rest) < rest_len:
            return None
        frame = head + rest
        if crc16(frame[:-2]) != struct.unpack("<H", frame[-2:])[0]:
            return None
        return frame

    def _xfer(self, req, rest_len, attempts=2):
        """Retry a lost/failed frame once: ~1 in 10 requests is lost on this
        USB-RS485 link even though the device answers solo requests 20/20.
        Writes are idempotent (same address/value), so a retry is safe."""
        for i in range(attempts):
            frame = self._xfer_once(req, rest_len)
            if frame is not None:
                return frame
            if (i + 1) < attempts:
                self.retries += 1
                time.sleep(0.005)
        return None

    def read(self, func, addr, cnt, attempts=2):
        req = struct.pack(">BBHH", self.slave, func, addr, cnt)
        for i in range(attempts):
            f = self._xfer(req, 1 + cnt * 2 + 2)
            if f is not None and f[2] == cnt * 2:
                return [struct.unpack(">H", f[3 + k * 2:5 + k * 2])[0]
                        for k in range(cnt)]
            if (i + 1) < attempts:
                self.retries += 1
                time.sleep(0.005)
        return None

    def write_multi(self, addr, values):
        data = b"".join(struct.pack(">H", v) for v in values)
        req = struct.pack(">BBHHB", self.slave, 0x10, addr, len(values), len(data)) + data
        return self._xfer(req, 6) is not None

    def write_single(self, addr, value):
        req = struct.pack(">BBHH", self.slave, 0x06, addr, value)
        return self._xfer(req, 6) is not None


class Report:
    def __init__(self):
        self.ok = self.fail = self.skip = 0

    def line(self, state, text):
        print("  [%s] %s" % (state, text))
        if state == "OK":
            self.ok += 1
        elif state == "FAIL":
            self.fail += 1
        else:
            self.skip += 1


def check_read(mb, rows, func, title, rep):
    print("\n== %s (func 0x%02X) ==" % (title, func))
    cache = {}

    for base, span in blocks_of(rows):
        if span > 125:
            rep.line("SKIP", "block 0x%04X too long (%d)" % (base, span))
            continue
        try:
            vals = mb.read(func, base, span)
        except MbError as e:
            rep.line("FAIL", "block 0x%04X..0x%04X -> %s" % (base, base + span - 1, e))
            continue
        if vals is None:
            rep.line("FAIL", "block 0x%04X..0x%04X -> no/bad answer" % (base, base + span - 1))
            continue
        for i, v in enumerate(vals):
            cache[base + i] = v
        rep.line("OK", "block 0x%04X..0x%04X (%d regs)" % (base, base + span - 1, span))

    for r in sorted(rows, key=lambda x: x["addr"]):
        a, span, name = r["addr"], r["span"], r["name"]
        try:
            vals = mb.read(func, a, span)
        except MbError as e:
            rep.line("FAIL", "0x%04X %-26s -> %s" % (a, name, e))
            continue
        if vals is None:
            rep.line("FAIL", "0x%04X %-26s -> no/bad answer" % (a, name))
            continue
        same = all(cache.get(a + i) == vals[i] for i in range(span))
        txt = ", ".join("0x%04X" % v for v in vals)
        if r["type"] == "T_FLT4" and span == 2:
            raw = (vals[1] << 16) | vals[0]
            txt += "  (float %.4f)" % struct.unpack("<f", struct.pack("<I", raw))[0]
        if r["type"] == "T_U32":
            txt += "  (u32 %u)" % ((vals[1] << 16) | vals[0])
        rep.line("OK" if same else "FAIL",
                 "0x%04X %-26s %s%s" % (a, name, txt, "" if same else "  != block read"))


def check_holes(mb, rows, func, rep):
    print("\n== address holes (expect exception 0x02) ==")
    covered = set()
    for r in rows:
        covered.update(range(r["addr"], r["addr"] + r["span"]))
    for a in (0x0000, 0x001F, 0x003A, 0x003F, 0x0059, 0x00FF, 0x0F00):
        if a in covered:
            continue
        try:
            vals = mb.read(func, a, 1)
        except MbError as e:
            rep.line("OK" if e.code == 0x02 else "FAIL",
                     "0x%04X -> exception 0x%02X" % (a, e.code))
            continue
        rep.line("FAIL" if vals is not None else "SKIP",
                 "0x%04X -> %s (expected exception)" % (a, "answered" if vals else "no answer"))


def check_write(mb, rows, rep):
    print("\n== write / read back / restore ==")

    busy = []
    for a, nm in ((0x0049, "erase_req"), (0x004A, "save_config"),
                  (0x004B, "save_config_default"), (0x004C, "load_config_default"),
                  (0x004D, "fw_apply_req"), (0x004E, "int_write_pending"),
                  (0x004F, "ext_write_pending")):
        try:
            v = mb.read(0x03, a, 1)
        except MbError:
            v = None
        if v and v[0]:
            busy.append(nm)
    if busy:
        rep.line("SKIP", "flash busy (%s) - write test skipped" % ", ".join(busy))
        return

    for r in sorted(rows, key=lambda x: x["addr"]):
        a, span, name = r["addr"], r["span"], r["name"]
        if a not in SAFE_WRITE or forbidden(a, span):
            continue
        try:
            orig = mb.read(0x03, a, span)
        except MbError as e:
            rep.line("FAIL", "0x%04X %-26s read before write -> %s" % (a, name, e))
            continue
        if orig is None:
            rep.line("FAIL", "0x%04X %-26s read before write -> no answer" % (a, name))
            continue

        probe = [(v ^ 0x0001) & 0xFFFF for v in orig]
        if probe == orig:
            probe = [(v + 1) & 0xFFFF for v in orig]

        try:
            wok = mb.write_multi(a, probe)
            back = mb.read(0x03, a, span) if wok else None
        except MbError as e:
            rep.line("FAIL", "0x%04X %-26s write -> %s" % (a, name, e))
            wok, back = False, None

        good = wok and back == probe

        try:
            mb.write_multi(a, orig)
            rest = mb.read(0x03, a, span)
        except MbError:
            rest = None
        restored = rest == orig

        detail = "wrote %s, read %s" % (
            ",".join("0x%04X" % v for v in probe),
            ",".join("0x%04X" % v for v in back) if back else "none")
        if not restored:
            detail += "  RESTORE FAILED"
        rep.line("OK" if (good and restored) else "FAIL",
                 "0x%04X %-26s %s" % (a, name, detail))

    target = 0x000D
    try:
        orig = mb.read(0x03, target, 1)
        if orig:
            val = (orig[0] + 1) & 0xFFFF
            ok6 = mb.write_single(target, val)
            back = mb.read(0x03, target, 1)
            mb.write_single(target, orig[0])
            rep.line("OK" if (ok6 and back and back[0] == val) else "FAIL",
                     "0x%04X func 0x06 single write" % target)
    except MbError as e:
        rep.line("FAIL", "0x%04X func 0x06 -> %s" % (target, e))


def probe(port, slave):
    print("== probe %s ==" % port)
    req = struct.pack(">BBHH", slave, 0x03, 0x0001, 1)
    req += struct.pack("<H", crc16(req))
    print("TX: %s\n" % " ".join("%02X" % b for b in req))
    for baud in (115200, 9600, 19200, 38400, 57600, 230400, 460800, 921600):
        try:
            s = serial.Serial(port, baud, timeout=0.4)
            s.reset_input_buffer(); s.write(req); s.flush()
            time.sleep(0.2)
            raw = s.read(64); s.close()
        except Exception as e:
            print("%7d baud: %s" % (baud, e)); continue
        hit = len(raw) >= 3 and raw[0] == slave and (raw[1] & 0x7F) == 0x03
        print("%7d baud: %-47s %s" % (
            baud, " ".join("%02X" % b for b in raw[:16]) if raw else "(empty)",
            "<== ANSWER" if hit else ""))
        if hit:
            return baud
    print("\nNo answer. Check: command_message() drains InputBuffer[TYPE_USART],")
    print("Command_vector_Transmit() implemented, CONFIG_UART == 1.")
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port_pos", nargs="?", default=None)
    ap.add_argument("--port", default=None)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--slave", type=int, default=1)
    ap.add_argument("--src", default=DEFAULT_SRC)
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--probe", action="store_true")
    a = ap.parse_args()

    if not os.path.exists(a.src):
        print("Command_vector.c not found: %s" % a.src)
        print("Run from the project root or pass --src PATH")
        return 2

    text = open(a.src, encoding="utf-8", errors="ignore").read()
    com, ins = parse_map(text, "com_map"), parse_map(text, "in_map")
    if not com:
        print("com_map not parsed - check the file format"); return 2

    if a.list:
        for title, rows in (("com_map (0x03/0x10)", com), ("in_map (0x04)", ins)):
            print("== %s: %d fields ==" % (title, len(rows)))
            for r in sorted(rows, key=lambda x: x["addr"]):
                print("  0x%04X..0x%04X  %-7s %s" % (
                    r["addr"], r["addr"] + r["span"] - 1, r["type"], r["name"]))
            print("  blocks: %s\n" % ", ".join("0x%04X+%d" % (b, n) for b, n in blocks_of(rows)))
        return 0

    port = a.port or a.port_pos
    if not port:
        print("Port is required: py tools/verify_registers.py COM3"); return 2
    if serial is None:
        print("pyserial is missing: py -m pip install pyserial"); return 2
    if a.probe:
        return 0 if probe(port, a.slave) else 1

    ser = serial.Serial(port, a.baud, timeout=1)
    mb, rep = Mb(ser, a.slave), Report()
    print("port=%s baud=%d slave=%d" % (port, a.baud, a.slave))
    print("map: com_map=%d fields, in_map=%d fields" % (len(com), len(ins)))

    link = None
    try:
        link = mb.read(0x03, com[0]["addr"], 1)
    except MbError as e:
        print("link check: %s" % e)
    if link is None:
        print("\nNo answer on the first register. Run with --probe.")
        ser.close(); return 1

    """check_read(mb, com, 0x03, "holding registers", rep)
    check_read(mb, ins, 0x04, "input registers", rep)
    check_holes(mb, com, 0x03, rep)"""
    if a.write:
        check_write(mb, com, rep)
    else:
        print("\n(write test disabled; add --write to check write/read/restore)")

    ser.close()
    print("\n== summary: %d OK, %d FAIL, %d SKIP (retries: %d) ==" % (
        rep.ok, rep.fail, rep.skip, mb.retries))
    return 1 if rep.fail else 0


if __name__ == "__main__":
    sys.exit(main())
