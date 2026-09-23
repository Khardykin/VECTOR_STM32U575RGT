#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
pack_sounds.py — собирает sounds.img для внешней SPI flash (MX25K6435F, 8 МБ)
и генерирует сопутствующие файлы, по которым видно, ЧТО и В КАКОМ ПОРЯДKE собрано.

Формат образа описан в Core/Inc/audio_image.h и ДОЛЖЕН совпадать с ним байт в байт:
прошивка читает заголовок и таблицу прямо из флеш, C-массивы не нужны.

Что создаётся при сборке:
  <out>                  сам образ sounds.img
  <out>.manifest.txt     паспорт сборки: порядок файлов, их sha256, rate, gain,
                         sha256 результата и готовая команда для воспроизведения.
                         Именно по нему видно, почему два образа совпали/различаются.
  Core/Inc/audio_ids.h   enum SND_<ИМЯ> = порядковый номер + SND_COUNT,
                         таблица-комментарий (номер, имя, сэмплы, байты, Гц, секунды)
                         и SND_STATE_DEFAULT_0/1 для таблицы состояний плеера.
                         Путь задаётся --ids-out (по умолчанию Core/Inc/audio_ids.h).

Использование:
    python3 tools/pack_sounds.py tools/*.wav --out tools/sounds.img
    python3 tools/pack_sounds.py tools/a.wav tools/b.wav --out s.img --rate 8000 --gain 3.0
    python3 tools/pack_sounds.py tools/b_click.wav --info      # параметры WAV без сборки
    python3 tools/pack_sounds.py tools/sounds.img --check      # распарсить готовый образ

Детерминизм: один и тот же список файлов В ТОМ ЖЕ ПОРЯДКЕ с теми же --rate/--gain
даёт образ байт в байт. Разный gain или другой порядок => разные байты;
сравнивать образы имеет смысл только вместе с manifest-файлами.

Требования к WAV: 16 бит/сэмпл. Каналы сводятся в моно, частота приводится к --rate
(по умолчанию 8000 — под текущую настройку SAI1).

Бюджет: 8 МБ = 8388608 байт = 524 с при 8 кГц/16 бит/моно.
"""
import argparse
import array
import hashlib
import os
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
DEFAULT_IDS = Path("Core/VectorLib/Audio/Inc/audio_ids.h")


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


def sanitize(stem):
    return "".join(c if c.isalnum() else "_" for c in stem).strip("_").upper() or "SOUND"


# --------------------------------------------------------------------------- #
def build(wavs, rate, gain):
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
            "enum": sanitize(w.stem),
            "pcm": pcm,
            "samples": len(s),
            "rate": rate,
            "ch": 1,
            "dur": len(s) / rate,
            "clip": clip,
            "src": str(w),
            "src_sha": hashlib.sha256(w.read_bytes()).hexdigest(),
            "src_rate": fr,
            "src_ch": nch,
        })
        print(f"[i] {w.name}: {nch}ch {fr}Гц -> 1ch {rate}Гц, {len(s)} сэмплов, "
              f"{len(s) / rate:.2f} с" + (f", клиппинг {clip}" if clip else ""),
              file=sys.stderr)

    entries, blobs = [], []
    data_off = HEADER_SIZE + ENTRY_SIZE * len(placeholders)
    addr = data_off
    for p in placeholders:
        pad = (-len(p["pcm"])) % 4
        entries.append({
            "offset": addr, "length": len(p["pcm"]), "samples": p["samples"],
            "rate": p["rate"], "ch": p["ch"],
            "crc": zlib.crc32(p["pcm"]) & 0xFFFFFFFF, "name": p["name"],
            "enum": p["enum"], "dur": p["dur"],
        })
        blobs.append(p["pcm"] + b"\x00" * pad)
        addr += len(p["pcm"]) + pad

    tbl = b""
    for e in entries:
        tbl += struct.pack("<IIIHBB", e["offset"], e["length"], e["samples"],
                           e["rate"], e["ch"], FMT_PCM16)
        tbl += struct.pack("<I", e["crc"])
        tbl += e["name"].encode("utf-8")[:NAME_LEN].ljust(NAME_LEN, b"\x00")
    assert len(tbl) == ENTRY_SIZE * len(entries)

    total = data_off + sum(len(b) for b in blobs)
    img = struct.pack("<IHHIIII", MAGIC, VERSION, len(entries), HEADER_SIZE,
                      data_off, total, zlib.crc32(tbl) & 0xFFFFFFFF) + tbl + b"".join(blobs)
    assert len(img) == total
    return img, entries, placeholders, total


# --------------------------------------------------------------------------- #
def emit_ids(entries, img_sha, out_img, path: Path):
    lines = [
        "/**",
        "  * @file    audio_ids.h",
        "  * @brief   СГЕНЕРИРОВАНО tools/pack_sounds.py — НЕ редактировать вручную.",
        "  *",
        f"  *          Образ: {out_img}  sha256 {img_sha}",
        "  *          Порядковый номер звука = порядок файла в команде сборки.",
        "  *          Вызывайте audio_play(SND_ИМЯ) — порядок не потеряется.",
        "  *",
        "  *          idx  имя             сэмплов     байт     Гц   секунд",
    ]
    for i, e in enumerate(entries):
        lines.append(f"  *          {i:3d}  {e['name']:<15s} {e['samples']:7d} {e['length']:8d}"
                     f" {e['rate']:6d} {e['dur']:7.2f}")
    lines += ["  */", "#ifndef AUDIO_IDS_H", "#define AUDIO_IDS_H", ""]
    lines.append("enum {")
    for i, e in enumerate(entries):
        lines.append(f"  SND_{e['enum']} = {i},")
    lines.append(f"  SND_COUNT = {len(entries)}")
    lines.append("};")
    lines.append("")
    lines.append("/* Значения по умолчанию для таблицы состояний плеера")
    lines.append("   (ap_state_map в audio_player.c). Если звуков меньше двух,")
    lines.append("   недостающие состояния = тишина (0xFFFF). */")
    lines.append(f"#define SND_STATE_DEFAULT_0  {('SND_' + entries[0]['enum']) if len(entries) > 0 else '0xFFFFu'}")
    d1 = ('SND_' + entries[1]['enum']) if len(entries) > 1 else '0xFFFFu'
    c1 = '  /* звуков меньше двух - тишина */' if len(entries) < 2 else ''
    lines.append(("#define SND_STATE_DEFAULT_1  " + d1 + c1).rstrip())
    lines.append("")
    lines.append("#endif /* AUDIO_IDS_H */")
    lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")
    print(f"[+] {path}: enum на {len(entries)} звук(ов)", file=sys.stderr)


def emit_manifest(placeholders, entries, rate, gain, out_img: Path, img: bytes, path: Path):
    sha = hashlib.sha256(img).hexdigest()
    L = [
        "# manifest сборки sounds.img (tools/pack_sounds.py)",
        f"out        = {out_img}",
        f"out_bytes  = {len(img)}",
        f"out_sha256 = {sha}",
        f"rate       = {rate}",
        f"gain       = {gain}",
        "",
        "# звуки в порядке сборки (этот порядок = индексы в прошивке):",
    ]
    for i, (p, e) in enumerate(zip(placeholders, entries)):
        L.append(f"  {i}  {p['src'].replace(chr(92), '/')}")
        L.append(f"      sha256={p['src_sha']}  src={p['src_ch']}ch {p['src_rate']}Гц"
                 f"  -> {e['samples']} сэмплов {e['rate']}Гц, {e['dur']:.2f} с")
    L += [
        "",
        "# команда для ТОЧНОГО воспроизведения этой сборки:",
        f"python tools/pack_sounds.py " +
        " ".join(p['src'].replace(chr(92), '/') for p in placeholders) +
        f" --rate {rate} --gain {gain} --out {out_img}",
        "",
    ]
    path.write_text("\n".join(L), encoding="utf-8")
    print(f"[+] {path}: паспорт сборки (sha256 {sha[:16]}…)", file=sys.stderr)
    return sha


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
    ap.add_argument("--ids-out", type=Path, default=DEFAULT_IDS,
                    help="куда писать сгенерированный enum-заголовок")
    ap.add_argument("--no-manifest", action="store_true")
    ap.add_argument("--check", action="store_true", help="распарсить готовый образ")
    ap.add_argument("--info", action="store_true",
                    help="показать параметры WAV без сборки")
    args = ap.parse_args()

    # --- отсев мусорных аргументов от переноса строки -----------------------
    # В cmd.exe/PowerShell символ '\' НЕ переносит строку (это bash-приём),
    # поэтому команда вида
    #     python tools/pack_sounds.py a.wav b.wav \
    #         --rate 16000 --out s.img
    # в Windows превращает '\' в отдельный аргумент, и wave.open падает с
    # FileNotFoundError: '\\'. В cmd.exe перенос - это '^', в PowerShell - '`'.
    # Молча отбрасываем такие "аргументы" и пишем подсказку.
    _junk = {"\\", "^", "`", "|", "&"}
    _wav = []
    for a in args.wav:
        sa = str(a).strip()
        if sa in _junk or sa == "":
            print(f"[!] пропущен аргумент {sa!r} - это символ переноса строки. "
                  f"В Windows пишите команду в ОДНУ строку (перенос в cmd.exe - "
                  f"'^', в PowerShell - '`', а '\\' работает только в bash).")
            continue
        _wav.append(Path(sa))
    if not _wav:
        sys.exit("[!] не осталось ни одного WAV-файла после разбора аргументов")
    args.wav = _wav

    # --- понятная ошибка вместо traceback, если файла нет ---
    for a in args.wav:
        if not a.exists():
            sys.exit(f"[!] файл не найден: {a}\n"
                     f"    Текущий каталог: {os.getcwd()}\n"
                     f"    Запускайте из корня проекта: python tools/pack_sounds.py tools/x.wav ...")

    # --- раскрытие масок (*.wav): cmd.exe и PowerShell не раскрывают '*' ---
    import glob as _glob
    expanded = []
    for a in args.wav:
        sa = str(a)
        if any(c in sa for c in "*?[") and not os.path.exists(sa):
            m = sorted(_glob.glob(sa))
            if not m:
                sys.exit(f"[!] по маске {sa} ничего не найдено")
            expanded += [Path(x) for x in m]
        else:
            expanded.append(Path(sa))
    if not expanded:
        sys.exit("[!] не указано ни одного WAV-файла")
    args.wav = expanded

    if args.check or (len(args.wav) == 1 and args.wav[0].suffix == ".img"):
        parse(args.wav[0].read_bytes())
        return

    if args.info:
        for w in args.wav:
            nch, fr, s = read_wav(w)
            frames = len(s) // nch
            peak = max(max(abs(min(s)), 1), max(s))
            print(f"{w.name}: {nch} ch, {fr} Гц, 16 бит, {frames} кадров, "
                  f"{frames / fr:.3f} с, пик {peak} ({100 * peak / 32768:.1f}% FS)")
        return

    if not args.out:
        sys.exit("[!] нужен --out для сборки")

    img, entries, placeholders, total = build(args.wav, args.rate, args.gain)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(img)
    sha = hashlib.sha256(img).hexdigest()

    print(f"\n[+] {args.out}: {total} байт ({total / 1024:.1f} КБ, "
          f"{100 * total / FLASH_SIZE:.2f}% от 8 МБ)", file=sys.stderr)
    secs = sum(e["length"] for e in entries) / (args.rate * 2)
    print(f"[+] суммарно аудио: {secs:.1f} с; свободно останется "
          f"{(FLASH_SIZE - total) / 1024:.0f} КБ = {(FLASH_SIZE - total) / (args.rate * 2):.0f} с",
          file=sys.stderr)
    if total > FLASH_SIZE:
        sys.exit("[!] образ не влезает в 8 МБ")

    emit_ids(entries, sha, args.out, args.ids_out)
    if not args.no_manifest:
        emit_manifest(placeholders, entries, args.rate, args.gain, args.out, img,
                      args.out.with_suffix(args.out.suffix + ".manifest.txt"))

    print("[i] контрольное чтение образа:")
    parse(img)


if __name__ == "__main__":
    main()
