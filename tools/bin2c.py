#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bin2c.py — пакует ЛЮБОЙ бинарник в C-массив const uint8_t.

Основное применение сейчас: зашить sounds.img во внутреннюю flash, чтобы
прошивка сама могла запрограммировать внешнюю (audio_factory.c):

    python tools\\bin2c.py tools\\sounds.img --name vector_factory_img ^
           --out Core\\VectorLib\\Audio\\Src\\audio_factory_image.c

После этого в прошивке появляются:
    const uint8_t  vector_factory_img[];
    const uint32_t vector_factory_img_size;
и при старте (поток плеера) образ автоматически уйдёт во внешнюю flash,
если там его нет или он битый (см. VECTOR_AUDIO_FACTORY_EMBED).
"""
import argparse
import sys
from pathlib import Path


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", type=Path)
    ap.add_argument("--name", default="vector_factory_img")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--per-line", type=int, default=16)
    args = ap.parse_args()

    data = args.src.read_bytes()
    if not data:
        sys.exit(f"[!] {args.src}: пустой файл")

    L = [
        "/* Сгенерировано tools/bin2c.py — НЕ редактировать вручную. */",
        f"/* источник: {args.src.name}, {len(data)} байт */",
        "#include <stdint.h>",
        "",
        f"const uint8_t {args.name}[] = {{",
    ]
    for i in range(0, len(data), args.per_line):
        chunk = data[i:i + args.per_line]
        L.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) +
                 ("," if i + args.per_line < len(data) else ""))
    L.append("};")
    L.append("")
    L.append(f"const uint32_t {args.name}_size = {len(data)}u;")
    L.append("")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(L), encoding="utf-8")
    print(f"[+] {args.out}: {len(data)} байт -> массив {args.name}[] "
          f"({len(data) / 1024:.1f} КБ внутренней flash)")


if __name__ == "__main__":
    main()
