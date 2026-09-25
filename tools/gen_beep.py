#!/usr/bin/env python3
"""
Генератор аварийного писка: Core/VectorLib/Audio/Src/audio_beep.c

  python3 tools/gen_beep.py [--rate 16000] [--freq 1200] [--ms 100] [--gap 40]
                            [--amp 18000] [--out Core/VectorLib/Audio/Src/audio_beep.c]

Зачем отдельный файл, а не wav в образе: писк должен работать, даже если
внешняя SPI flash мертва или образ в ней битый, поэтому он живёт константой во
внутренней flash и не зависит ни от чего.

ВАЖНО: частота --rate обязана совпадать с настройкой SAI1 (CubeMX ->
SAI1 -> Audio Frequency) и с --rate сборки образа (tools/pack_sounds.py).
Иначе писк проиграется с другой скоростью и высотой тона.

audio_beep_samples всегда равно числу элементов массива - раньше они
разъехались (96 против 1920) и DMA читала мусор за концом массива.
"""
import argparse, math, re
from pathlib import Path

TPL = """/**
  ******************************************************************************
  * @file    audio_beep.c
  * @brief   АВАРИЙНЫЙ писк во внутренней flash (не зависит от внешней памяти)
  *
  *          {ntones} тона(ов) {freq} Гц по {ms} мс с зазором {gap} мс, {rate} Гц, моно, 16 бит,
  *          амплитуда +-{amp} ({pct}% шкалы), фронты по 4 мс чтобы не щёлкало.
  *          Итого {n} сэмплов = {dur} мс = {kb} КБ внутренней flash.
  *          Играется, если:
  *            - образ sounds.img во внешней flash не найден/битый (авто-фолбэк)
  *            - вызван явно audio_beep() или audio_selftest()
  *          Сгенерирован tools/gen_beep.py; руками не править.
  *
  *          ВАЖНО: audio_beep_samples ОБЯЗАНО равняться числу элементов
  *          audio_beep_pcm[].
  ******************************************************************************
  */
#include "audio_beep.h"

const int16_t audio_beep_pcm[] = {{
{body}
}};

/* Ровно столько элементов, сколько в массиве выше ({n}). */
const uint32_t audio_beep_samples = {n}u;   /* {dur} мс @ {rate} Гц */
"""


def tone(rate, freq, ms, amp):
    n = int(rate * ms / 1000)
    fade = max(1, int(rate * 0.004))
    out = []
    for i in range(n):
        a = 1.0
        if i < fade:
            a = i / fade
        elif i >= n - fade:
            a = (n - i) / fade
        out.append(int(round(amp * a * math.sin(2 * math.pi * freq * i / rate))))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rate", type=int, default=16000, help="частота SAI/образа, Гц")
    ap.add_argument("--freq", type=float, default=1200.0, help="частота тона, Гц")
    ap.add_argument("--ms", type=int, default=100, help="длительность одного тона, мс")
    ap.add_argument("--gap", type=int, default=40, help="зазор между тонами, мс")
    ap.add_argument("--ntones", type=int, default=2, help="сколько тонов")
    ap.add_argument("--amp", type=int, default=18000, help="амплитуда (0..32767)")
    ap.add_argument("--out", type=Path,
                    default=Path("Core/VectorLib/Audio/Src/audio_beep.c"))
    a = ap.parse_args()

    pcm = []
    for k in range(a.ntones):
        if k:
            pcm += [0] * int(a.rate * a.gap / 1000)
        pcm += tone(a.rate, a.freq, a.ms, a.amp)

    lines = []
    n = len(pcm)
    for i in range(0, n, 16):
        lines.append("  " + ", ".join(str(v) for v in pcm[i:i + 16])
                     + ("," if i + 16 < n else ""))

    txt = TPL.format(ntones=a.ntones, freq=int(a.freq), ms=a.ms, gap=a.gap,
                     rate=a.rate, amp=a.amp, pct=round(100 * a.amp / 32768),
                     n=n, dur=n * 1000 // a.rate, kb=(n * 2 + 1023) // 1024,
                     body="\n".join(lines))
    a.out.write_text(txt, encoding="utf-8")

    # самопроверка: число элементов массива == audio_beep_samples
    vals = re.findall(r'-?\d+', txt[txt.index("audio_beep_pcm[] = {"):txt.index("};")])
    assert len(vals) == n, (len(vals), n)
    print(f"[+] {a.out}: {n} сэмплов, {n*1000//a.rate} мс, {n*2} байт, "
          f"{a.rate} Гц, тон {int(a.freq)} Гц")


if __name__ == "__main__":
    main()
