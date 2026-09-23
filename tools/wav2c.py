#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
wav2c.py — конвертер WAV -> C-массив ГОЛЫХ PCM-данных (без RIFF/fmt/LIST-заголовка).

Два режима работы.

1) ОДИН ФАЙЛ (как раньше) — массив в stdout или в --out:
     python3 tools/wav2c.py assets/gas.wav --name sound_gas_warning \
                            --out Core/Src/audio_samples.c
     python3 tools/wav2c.py assets/gas.wav --info        # только показать параметры

2) БАТЧ — по одному .c на каждый звук + ОБЩИЙ заголовок с таблицей дескрипторов:
     python3 tools/wav2c.py assets/*.wav \
         --outdir Core/Src --header Core/Inc/audio_samples.h --prefix audio_snd_

   Получится:
     Core/Src/audio_snd_gas.c          -> const uint16_t snd_gas[]; ..._size;
     Core/Src/audio_snd_click.c        -> const uint16_t snd_click[]; ..._size;
     Core/Inc/audio_samples.h          -> extern'ы, enum SND_*, таблица audio_sounds[]

   Сама таблица (audio_sounds[]) объявляется в заголовке как extern, а её
   определение печатается отдельным файлом --table-out (обычно Core/Src/audio_table.c).
   Если --table-out не задан, определение печатается в stdout.

Ключи:
  --gain 3.0        линейное усиление (с защитой от клиппинга)
  --rate 8000       передискретизировать в 8 кГц -> вдвое меньше флеша
  --stereo          дублировать моно в оба канала (если SAI в стерео)
  --signed-decimal  выдавать int16_t десятичными числами (по умолчанию uint16_t hex)
  --info            ничего не писать, только параметры файла

Ограничение HAL: Size в HAL_SAI_Transmit_DMA() имеет тип uint16_t,
т.е. за одну передачу максимум 65535 сэмплов (4.1 с при 16 кГц, 8.2 с при 8 кГц).
Для более длинных файлов нужен чанкинг или linked-list DMA
(GPDMA1_Channel12 в проекте уже настроен под DMA_LINKEDLIST_CIRCULAR, но не используется).
"""
import argparse
import os
import array
import re
import sys
import wave
from pathlib import Path

UINT16_MAX_TRANSFER = 65535  # лимит параметра Size в HAL_SAI_Transmit_DMA()


# --------------------------------------------------------------------------- #
#  чтение и обработка
# --------------------------------------------------------------------------- #
def read_wav(path: Path):
    """-> (nch, rate, [int16 samples interleaved])"""
    with wave.open(str(path), "rb") as w:
        nch, sw, fr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if sw != 2:
        sys.exit(f"[!] {path}: нужен 16 бит/сэмпл, в файле {sw * 8} бит")
    pcm = array.array("h")
    pcm.frombytes(raw)
    if sys.byteorder == "big":
        pcm.byteswap()
    return nch, fr, list(pcm)


def to_mono(samples, nch):
    if nch == 1:
        return samples
    frames = len(samples) // nch
    out = [0] * frames
    for i in range(frames):
        acc = 0
        for c in range(nch):
            acc += samples[i * nch + c]
        out[i] = acc // nch
    return out


def to_stereo(samples):
    out = [0] * (2 * len(samples))
    out[0::2] = samples
    out[1::2] = samples
    return out


def resample(samples, src_rate, dst_rate):
    """Линейная интерполяция. Для сигналов/сирен качества хватает;
       для музыки лучше передискретизировать в Audacity (там нормальный ФНЧ)."""
    if src_rate == dst_rate:
        return samples
    n_out = int(round(len(samples) * dst_rate / src_rate))
    if n_out <= 1:
        return samples[:1]
    ratio = (len(samples) - 1) / (n_out - 1)
    out = [0] * n_out
    for i in range(n_out):
        x = i * ratio
        i0 = int(x)
        i1 = min(i0 + 1, len(samples) - 1)
        f = x - i0
        out[i] = int(round(samples[i0] * (1.0 - f) + samples[i1] * f))
    return out


def apply_gain(samples, gain):
    out, clip = [], 0
    for s in samples:
        v = int(round(s * gain))
        if v > 32767:
            v, clip = 32767, clip + 1
        elif v < -32768:
            v, clip = -32768, clip + 1
        out.append(v)
    return out, clip


def remove_dc(samples):
    if not samples:
        return samples
    dc = sum(samples) / len(samples)
    if abs(dc) < 0.5:
        return samples
    return [max(-32768, min(32767, int(round(s - dc)))) for s in samples]


def process(path: Path, args):
    """Полный конвейер: чтение -> моно -> ресэмпл -> DC -> gain -> стерео."""
    nch, fr, samples = read_wav(path)
    info = {"src_ch": nch, "src_rate": fr, "src_frames": len(samples) // nch}

    if not args.keep_stereo and nch > 1:
        samples = to_mono(samples, nch)
        nch = 1
    if args.rate and args.rate != fr:
        samples = resample(samples, fr, args.rate)
        fr = args.rate
    if args.strip_dc:
        samples = remove_dc(samples)
    if args.gain != 1.0:
        samples, clip = apply_gain(samples, args.gain)
        info["clipped"] = clip
    if args.stereo:
        samples = to_stereo(samples)

    info.update(rate=fr, ch=2 if args.stereo else 1, samples=len(samples),
                dur=len(samples) / fr,
                peak=max(max(abs(min(samples)), 1), max(samples)) if samples else 0,
                dc=sum(samples) / len(samples) if samples else 0)
    return samples, info


# --------------------------------------------------------------------------- #
#  генерация C
# --------------------------------------------------------------------------- #
def emit_c(name, samples, per_line=16, hexfmt=True, comment=None):
    L = ["/* Сгенерировано tools/wav2c.py — НЕ редактировать вручную. */"]
    if comment:
        L.append(f"/* {comment} */")
    L.append("#include <stdint.h>")
    L.append("")
    L.append(f"const {'uint16_t' if hexfmt else 'int16_t'} {name}[] = {{")
    for i in range(0, len(samples), per_line):
        chunk = samples[i:i + per_line]
        body = ", ".join(f"0x{v & 0xFFFF:04x}" if hexfmt else f"{v}" for v in chunk)
        L.append("  " + body + ("," if i + per_line < len(samples) else ""))
    L.append("};")
    L.append("")
    L.append(f"const uint32_t {name}_size = {len(samples)}u;  /* ЧИСЛО СЭМПЛОВ, не байт */")
    L.append("")
    return "\n".join(L)


def sanitize(stem):
    return re.sub(r"[^A-Za-z0-9_]", "_", stem).strip("_") or "sound"


# --------------------------------------------------------------------------- #
#  заголовок + таблица
# --------------------------------------------------------------------------- #
HDR_TEMPLATE = """/**
  ******************************************************************************
  * @file    {hdr_name}
  * @brief   Звуковые ресурсы. Сгенерировано tools/wav2c.py — НЕ редактировать
  *          вручную, иначе правки потеряются при следующей генерации.
  *
  *          Все массивы — чистые PCM-данные БЕЗ WAV-заголовка.
  *          {n} звук(ов), суммарно {total_kb:.1f} КБ во флеше.
  ******************************************************************************
  */
#ifndef AUDIO_SAMPLES_H
#define AUDIO_SAMPLES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

/* Дескриптор одного звука */
typedef struct {{
  const uint16_t *pcm;      /* указатель на сэмплы                    */
  uint32_t        samples;  /* ЧИСЛО сэмплов (НЕ байт)                */
  uint16_t        rate;     /* частота дискретизации, Гц              */
  uint8_t         channels; /* 1 = моно (SAI должен быть в MONOMODE)  */
  const char     *name;     /* для логов                              */
}} audio_sound_t;

/* Идентификаторы звуков — то, что передают в audio_play() */
enum {{
{enum_body}
  SND_COUNT
}};

/* Таблица определена в {table_file} */
extern const audio_sound_t audio_sounds[SND_COUNT];

{externs}

#ifdef __cplusplus
}}
#endif

#endif /* AUDIO_SAMPLES_H */
"""

TABLE_TEMPLATE = """/* Сгенерировано tools/wav2c.py — НЕ редактировать вручную. */
#include "{hdr_include}"

const audio_sound_t audio_sounds[SND_COUNT] = {{
{rows}
}};
"""


def make_header(entries, hdr_name, table_file):
    enum_body = "\n".join(f"  SND_{e['enum']} = {i}," if i == 0 else f"  SND_{e['enum']},"
                          for i, e in enumerate(entries))
    externs = "\n".join(
        f"extern const uint16_t {e['arr']}[];\n"
        f"extern const uint32_t {e['arr']}_size;   /* {e['info']['samples']} сэмплов, "
        f"{e['info']['dur']:.2f} с @ {e['info']['rate']} Гц */"
        for e in entries)
    total = sum(e["info"]["samples"] * 2 for e in entries)
    return HDR_TEMPLATE.format(hdr_name=hdr_name, n=len(entries), total_kb=total / 1024,
                               enum_body=enum_body, externs=externs, table_file=table_file)


def make_table(entries, hdr_include):
    rows = ",\n".join(
        f"  {{ {e['arr']}, {e['arr']}_size, {e['info']['rate']}u, "
        f"{e['info']['ch']}u, \"{e['label']}\" }}"
        for e in entries)
    return TABLE_TEMPLATE.format(hdr_include=hdr_include, rows=rows)


# --------------------------------------------------------------------------- #
def warn_limits(info, path):
    if info["samples"] > UINT16_MAX_TRANSFER:
        print(f"[!] {path.name}: {info['samples']} сэмплов > лимита uint16_t Size "
              f"({UINT16_MAX_TRANSFER}).\n    Нужен чанкинг или circular/linked-list DMA.",
              file=sys.stderr)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wav", type=Path, nargs="+", help="один или несколько WAV-файлов")
    ap.add_argument("--name", default=None, help="имя C-массива (режим одного файла)")
    ap.add_argument("--out", type=Path, default=None, help="куда писать .c (один файл)")
    ap.add_argument("--info", action="store_true", help="только показать параметры")

    g = ap.add_argument_group("батч-режим (несколько файлов)")
    g.add_argument("--outdir", type=Path, default=None,
                   help="папка для .c-файлов (по одному на звук)")
    g.add_argument("--header", type=Path, default=None,
                   help="путь общего заголовка, напр. Core/Inc/audio_samples.h")
    g.add_argument("--table-out", type=Path, default=None,
                   help="куда писать audio_sounds[], напр. Core/Src/audio_table.c")
    g.add_argument("--prefix", default="audio_snd_", help="префикс имён .c-файлов")
    g.add_argument("--arr-prefix", default="snd_", help="префикс имён C-массивов")

    p = ap.add_argument_group("обработка звука")
    p.add_argument("--gain", type=float, default=1.0, help="линейное усиление")
    p.add_argument("--rate", type=int, default=None,
                   help="передискретизировать (8000 = вдвое меньше флеша)")
    p.add_argument("--strip-dc", action="store_true", help="убрать постоянную составляющую")
    p.add_argument("--stereo", action="store_true", help="дублировать моно в оба канала")
    p.add_argument("--keep-stereo", action="store_true", help="не сводить стерео-WAV в моно")
    p.add_argument("--signed-decimal", action="store_true",
                   help="int16_t десятичными (по умолчанию uint16_t hex)")
    p.add_argument("--unsigned-hex", action="store_true",
                   help=argparse.SUPPRESS)   # устаревший алиас, hex и так по умолчанию
    args = ap.parse_args()

    # --- раскрытие масок (*.wav) своими руками -------------------------
    # cmd.exe и PowerShell НЕ раскрывают '*', в отличие от bash. Без этого
    # "pack_sounds.py assets\*.wav" падает с OSError: Invalid argument.
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
    hexfmt = not args.signed_decimal

    # ---- батч-режим ----
    if args.outdir or args.header or len(args.wav) > 1:
        if not args.outdir or not args.header:
            sys.exit("[!] для батч-режима нужны ОБА ключа: --outdir и --header")
        args.outdir.mkdir(parents=True, exist_ok=True)
        args.header.parent.mkdir(parents=True, exist_ok=True)

        entries = []
        for wav in args.wav:
            stem = sanitize(wav.stem)
            samples, info = process(wav, args)
            warn_limits(info, wav)
            arr = args.arr_prefix + stem
            label = wav.stem
            cfile = args.outdir / f"{args.prefix}{stem}.c"
            cfile.write_text(
                emit_c(arr, samples, hexfmt=hexfmt,
                       comment=f"источник: {wav.name}  |  {info['dur']:.2f} с, "
                               f"{info['rate']} Гц, {info['ch']} ch"),
                encoding="utf-8")
            print(f"[+] {cfile}  ({len(samples)} сэмплов, {len(samples) * 2 / 1024:.1f} КБ)",
                  file=sys.stderr)
            entries.append({"arr": arr, "enum": stem.upper(), "label": label, "info": info})

        table_file = args.table_out or (args.outdir / "audio_table.c")
        args.header.write_text(make_header(entries, args.header.name, table_file.name),
                               encoding="utf-8")
        table = make_table(entries, args.header.name)
        if args.table_out:
            args.table_out.parent.mkdir(parents=True, exist_ok=True)
            args.table_out.write_text(table, encoding="utf-8")
            print(f"[+] {args.table_out}", file=sys.stderr)
        else:
            sys.stdout.write(table)
        print(f"[+] {args.header}  ({len(entries)} звук(ов), "
              f"{sum(e['info']['samples'] * 2 for e in entries) / 1024:.1f} КБ)",
              file=sys.stderr)
        return

    # ---- один файл ----
    wav = args.wav[0]
    samples, info = process(wav, args)
    print(f"[i] {wav}: {info['src_ch']} ch {info['src_rate']} Гц -> "
          f"{info['ch']} ch {info['rate']} Гц, {info['samples']} сэмплов, "
          f"{info['dur']:.3f} с", file=sys.stderr)
    print(f"[i] пик = {info['peak']} ({100 * info['peak'] / 32768:.1f}% FS), "
          f"DC = {info['dc']:.1f}"
          + (f", клиппинг на {info['clipped']}" if "clipped" in info else ""),
          file=sys.stderr)
    warn_limits(info, wav)
    if args.info:
        return
    if info["rate"] != 16000:
        print(f"[!] Сэмпл-рейт {info['rate']} Гц != 16000 Гц, на который настроен SAI1.\n"
              f"    Либо --rate 16000, либо поменяйте SAI1.AudioFrequency в CubeMX\n"
              f"    (и пересчитайте PLL3: f_ker = 256 * Fs * MCKDIV).", file=sys.stderr)

    name = args.name or sanitize(wav.stem)
    text = emit_c(name, samples, hexfmt=hexfmt)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="utf-8")
        print(f"[+] {args.out} ({len(text)} байт)", file=sys.stderr)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
