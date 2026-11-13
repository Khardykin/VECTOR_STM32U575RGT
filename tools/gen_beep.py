#!/usr/bin/env python3
"""
Генератор аварийного писка: Core/VectorLib/Audio/Src/audio_beep.c

  python3 tools/gen_beep.py [--rate 44100] [--freq 1200] [--ms 100] [--gap 40]
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
  *            - образ sounds.bin во внешней flash не найден/битый (авто-фолбэк)
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


TPL_LOOP = """/**
  ******************************************************************************
  * @file    audio_beep.c
  * @brief   АВАРИЙНЫЙ писк во внутренней flash (не зависит от внешней памяти)
  *
  *          Писк ОДНОЙ частоты {freq} Гц, {ms} мс, {rate} Гц, моно, 16 бит,
  *          амплитуда +-{amp} ({pct}% шкалы). Хранится только КОРОТКАЯ БЕСШОВНАЯ
  *          ПЕТЛЯ ({n} сэмплов = {per} периода), плеер повторяет её по кругу до
  *          полной длины audio_beep_samples; fade-in/out 4 мс накладывает плеер.
  *          Петля: {n} сэмплов = {kb} байт внутренней flash (вместо {old} байт).
  *          Сгенерирован tools/gen_beep.py --loop; руками не править.
  *
  *          ВАЖНО: audio_beep_loop_samples ОБЯЗАНО равняться числу элементов
  *          audio_beep_pcm[].
  ******************************************************************************
  */
#include "audio_beep.h"

const int16_t audio_beep_pcm[] = {{
{body}
}};

/* Петля: ровно столько элементов, сколько в массиве выше ({n}). */
const uint32_t audio_beep_loop_samples = {n}u;   /* {per} периода {freq} Гц @ {rate} */
/* Полная длина писка: плеер повторяет петлю до этого числа сэмплов. */
const uint32_t audio_beep_samples = {tot}u;      /* {ms} мс @ {rate} Гц */
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
    ap.add_argument("--rate", type=int, default=44100, help="частота SAI/образа, Гц")
    ap.add_argument("--freq", type=float, default=1200.0, help="частота тона, Гц")
    ap.add_argument("--ms", type=int, default=100, help="длительность одного тона, мс")
    ap.add_argument("--gap", type=int, default=40, help="зазор между тонами, мс")
    ap.add_argument("--ntones", type=int, default=1, help="сколько тонов (1 = писк одной частоты)")
    ap.add_argument("--amp", type=int, default=18000, help="амплитуда (0..32767)")
    ap.add_argument("--out", type=Path,
                    default=Path("Core/VectorLib/Src/Audio/Src/audio_beep.c"))
    ap.add_argument("--loop", action="store_true",
                    help="писк одной частоты: короткая бесшовная петля + повтор плеером")
    a = ap.parse_args()

    if a.loop:
        loop_len = a.rate // math.gcd(a.rate, int(a.freq))
        pcm = [int(round(a.amp * math.sin(2 * math.pi * a.freq * i / a.rate)))
               for i in range(loop_len)]
        total = int(a.rate * a.ms / 1000)
        lines = []
        for i in range(0, loop_len, 16):
            lines.append("  " + ", ".join(str(v) for v in pcm[i:i + 16])
                         + ("," if i + 16 < loop_len else ""))
        txt = TPL_LOOP.format(freq=int(a.freq), ms=a.ms, rate=a.rate, amp=a.amp,
                              pct=round(100 * a.amp / 32768), n=loop_len,
                              per=loop_len * int(a.freq) // a.rate, kb=loop_len * 2,
                              old=total * 2, body="\n".join(lines), tot=total)
        a.out.write_text(txt, encoding="utf-8")
        vals = re.findall(r'-?\d+', txt[txt.index("audio_beep_pcm[] = {"):txt.index("};")])
        assert len(vals) == loop_len, (len(vals), loop_len)
        print(f"[+] {a.out}: петля {loop_len} сэмплов ({loop_len*2} байт), "
              f"полная длина {total} = {a.ms} мс, {a.rate} Гц, тон {int(a.freq)} Гц")
        return

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
