#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
pack_sounds.py — собирает sounds.img для внешней SPI flash (MX25K6435F, 8 МБ).

Формат образа описан в Core/Inc/audio_image.h и ДОЛЖЕН совпадать с ним байт в байт:
прошивка читает заголовок и таблицу прямо из флеш, C-массивы не нужны.

Использование:
    python3 tools/pack_sounds.py assets/*.wav --out build/sounds.img
    python3 tools/pack_sounds.py a.wav b.wav --out sounds.img --rate 8000 --gain 3.0
    python3 tools/pack_sounds.py sounds.img --check      # распарсить готовый образ

Требования к WAV: 16 бит/сэмпл. Каналы сводятся в моно, частота приводится к --rate
(по умолчанию 8000 — под текущую настройку SAI1). Длительность не ограничена
(в отличие от one-shot DMA): ограничение в 65535 сэмплов здесь не действует,
потому что воспроизведение идёт стримингом из флеш.

Бюджет: 8 МБ = 8388608 байт = 524 с при 8 кГц/16 бит/моно.
"""
import argparse
import array
import struct
import sys
import wave
import zlib
from pathlib import Path

MAGIC = 0x49444E53          # 'S','N','D','I' little-endian
VERSION = 1
HEADER_SIZE = 24
ENTRY_SIZE = 36
NAME_LEN = 16
FMT_PCM16 = 0
FLASH_SIZE = 8 * 1024 * 1024


# --------------------------------------------------------------------------- #
def read_wav(path: Path):
    with wave.open(str(path), "rb") as w:
        nch, sw, fr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if sw != 2:
        sys.exit(f"[!] {path}: нужен 16 бит/сэмпл, в файле {sw * 8}")
    pcm = array.array("h")
    pcm.frombytes(raw)
    if sys.byteorder == "big":
        pcm.byteswap()
    return nch, fr, list(pcm)


def to_mono(s, nch):
    if nch == 1:
        return s
    frames = len(s) // nch
    return [sum(s[i * nch + c] for c in range(nch)) // nch for i in range(frames)]


def resample(s, src, dst):
    if src == dst or not s:
        return s
    n_out = max(1, int(round(len(s) * dst / src)))
    ratio = (len(s) - 1) / max(1, n_out - 1)
    out = []
    for i in range(n_out):
        x = i * ratio
        i0 = int(x)
        i1 = min(i0 + 1, len(s) - 1)
        f = x - i0
        out.append(int(round(s[i0] * (1 - f) + s[i1] * f)))
    return out


def gain_clip(s, g):
    out, clip = [], 0
    for v in s:
        v = int(round(v * g))
        if v > 32767:
            v, clip = 32767, clip + 1
        elif v < -32768:
            v, clip = -32768, clip + 1
        out.append(v)
    return out, clip


# --------------------------------------------------------------------------- #
def build(wavs, rate, gain):
    entries, blobs = [], []
    cursor = 0  # заполнится после того, как узнаем data_off
    placeholders = []
    for w in wavs:
        nch, fr, s = read_wav(w)
        s = to_mono(s, nch)
        s = resample(s, fr, rate)
        clip = 0
        if gain != 1.0:
            s, clip = gain_clip(s, gain)
        pcm = struct.pack("<%dh" % len(s), *s) if s else b""
        placeholders.append({
            "name": w.stem[:NAME_LEN - 1],
            "pcm": pcm,
            "samples": len(s),
            "rate": rate,
            "ch": 1,
            "dur": len(s) / rate,
            "clip": clip,
            "src": w.name,
            "src_rate": fr,
        })
        print(f"[i] {w.name}: {nch}ch {fr}Гц -> 1ch {rate}Гц, {len(s)} сэмплов, "
              f"{len(s) / rate:.2f} с" + (f", клиппинг {clip}" if clip else ""),
              file=sys.stderr)

    data_off = HEADER_SIZE + ENTRY_SIZE * len(placeholders)
    addr = data_off
    for p in placeholders:
        pad = (-len(p["pcm"])) % 4
        entries.append({
            "offset": addr,
            "length": len(p["pcm"]),
            "samples": p["samples"],
            "rate": p["rate"],
            "ch": p["ch"],
            "crc": zlib.crc32(p["pcm"]) & 0xFFFFFFFF,
            "name": p["name"],
        })
        blobs.append(p["pcm"] + b"\x00" * pad)
        addr += len(p["pcm"]) + pad

    # собираем таблицу вручную, чтобы точно совпасть с audio_image.h
    tbl = b""
    for e in entries:
        tbl += struct.pack("<IIIHBB", e["offset"], e["length"], e["samples"],
                           e["rate"], e["ch"], FMT_PCM16)
        tbl += struct.pack("<I", e["crc"])
        tbl += e["name"].encode("utf-8")[:NAME_LEN].ljust(NAME_LEN, b"\x00")
    assert len(tbl) == ENTRY_SIZE * len(entries)

    total = data_off + sum(len(b) for b in blobs)
    hdr = struct.pack("<IHHIIII", MAGIC, VERSION, len(entries), HEADER_SIZE,
                      data_off, total, zlib.crc32(tbl) & 0xFFFFFFFF)
    assert len(hdr) == HEADER_SIZE
    return hdr + tbl + b"".join(blobs), entries, total


# --------------------------------------------------------------------------- #
def parse(img: bytes):
    magic, ver, cnt, t_off, d_off, total, tcrc = struct.unpack_from("<IHHIIII", img, 0)
    if magic != MAGIC:
        sys.exit(f"[!] bad magic {magic:#010x}, ожидался {MAGIC:#010x}")
    tbl = img[HEADER_SIZE:HEADER_SIZE + ENTRY_SIZE * cnt]
    calc = zlib.crc32(tbl) & 0xFFFFFFFF
    print(f"образ: version={ver} count={cnt} data_off={d_off} total={total} "
          f"table_crc={'OK' if calc == tcrc else f'БАД {calc:#010x} != {tcrc:#010x}'}")
    for i in range(cnt):
        o = HEADER_SIZE + i * ENTRY_SIZE
        off, ln, sm, rate, ch, fmt = struct.unpack_from("<IIIHBB", img, o)
        (crc,) = struct.unpack_from("<I", img, o + 16)
        name = img[o + 20:o + 36].split(b"\x00")[0].decode("utf-8", "replace")
        blob = img[off:off + ln]
        ok = "OK" if (zlib.crc32(blob) & 0xFFFFFFFF) == crc else "БАД"
        print(f"  [{i}] {name:16s} off={off:8d} len={ln:7d} {sm:6d} сэмплов "
              f"{rate}Гц {ch}ch fmt={fmt} crc={ok} ({ln / (rate * 2 * ch):.2f} с)")
    return cnt


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wav", type=Path, nargs="+")
    ap.add_argument("--out", type=Path, default=None)
    ap.add_argument("--rate", type=int, default=8000)
    ap.add_argument("--gain", type=float, default=1.0)
    ap.add_argument("--check", action="store_true", help="распарсить готовый образ")
    args = ap.parse_args()

    if args.check or (len(args.wav) == 1 and args.wav[0].suffix == ".img"):
        parse(args.wav[0].read_bytes())
        return
    if not args.out:
        sys.exit("[!] нужен --out для сборки")

    img, entries, total = build(args.wav, args.rate, args.gain)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(img)

    print(f"\n[+] {args.out}: {total} байт ({total / 1024:.1f} КБ, "
          f"{100 * total / FLASH_SIZE:.2f}% от 8 МБ)", file=sys.stderr)
    secs = sum(e["length"] for e in entries) / (args.rate * 2)
    print(f"[+] суммарно аудио: {secs:.1f} с; свободно останется "
          f"{(FLASH_SIZE - total) / 1024:.0f} КБ = {(FLASH_SIZE - total) / (args.rate * 2):.0f} с",
          file=sys.stderr)
    if total > FLASH_SIZE:
        sys.exit("[!] образ не влезает в 8 МБ")
    print("[i] контрольное чтение образа:")
    parse(img)


if __name__ == "__main__":
    main()
