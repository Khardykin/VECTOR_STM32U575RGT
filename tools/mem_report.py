#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
mem_report.py — таблица расхода RAM и FLASH по модулям (PLAN п.66, задача
автора: «кто сколько кушает»).

Читает linker .map файл сборки STM32CubeIDE/GNU ld (Debug/VECTOR_STM32U575RGT.map)
и раскладывает байты по object-файлам и модулям:

  FLASH = .isr_vector + .text + .rodata + .ARM.ex* + LMA-копия .data
  RAM   = .data + .bss + COMMON + ._user_heap_stack (резерв кучи/стека)

Динамической памяти в проекте нет (ThreadX byte pool не создаётся, malloc не
используется — стек каждого потока это статический массив .bss в Vector_main.c),
поэтому map-файл даёт ПОЛНУЮ картину: сумма таблицы = всё занятое RAM/FLASH.
Стеки потоков видны как .bss.<имя_массива> у объекта-владельца (раздел
«Топ символов RAM»).

Запуск (на машине с собранной прошивкой, python3 >= 3.8):
    python3 tools/mem_report.py Debug/VECTOR_STM32U575RGT.map
    python3 tools/mem_report.py Debug/*.map --top 40 --csv mem.csv
    python3 tools/mem_report.py --selftest        # проверка парсера без сборки

Ключи:
    --top N      строк в таблицах деталей (по умолч. 25)
    --csv FILE   дополнительно выгрузить пообъектную таблицу в CSV
    --selftest   прогнать парсер на синтетическом map-файле
"""
import argparse
import csv
import re
import sys

# ---------------------------------------------------------------------------
# Разбор map-файла GNU ld
# ---------------------------------------------------------------------------

HEX = r'0x[0-9a-fA-F]+'


def parse_memory_config(lines):
    """Memory Configuration -> {name: (origin, length)}."""
    regions = {}
    try:
        start = next(i for i, l in enumerate(lines) if l.startswith('Memory Configuration'))
    except StopIteration:
        return regions
    for l in lines[start + 1:]:
        if l.startswith('Linker script and memory map'):
            break
        m = re.match(r'^(\S+)\s+(%s)\s+(%s)' % (HEX, HEX), l)
        if m and m.group(1) != 'Name':
            regions[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
    return regions


def classify_addr(addr, regions):
    """'RAM' / 'FLASH' / None по адресу VMA (карта памяти Cortex-M33:
    FLASH 0x0800_0000.., SRAM 0x2000_0000..0x3FFF_FFFF вкл. SRAM4/BKPSRAM
    0x2800_0000; регионы из Memory Configuration используются только для
    справки — адреса секций всегда абсолютные)."""
    del regions
    if 0x08000000 <= addr < 0x20000000:
        return 'FLASH'
    if 0x20000000 <= addr < 0x60000000:
        return 'RAM'
    return None


def parse_map(path_or_text, is_text=False):
    """
    Возвращает список записей:
      dict(obj, module, out_sec, in_sec, addr, size, kind)
    kind: 'flash' | 'ram' | 'flash+ram' (.data: LMA во flash, VMA в RAM)
    """
    if is_text:
        text = path_or_text
    else:
        with open(path_or_text, 'r', encoding='utf-8', errors='replace') as f:
            text = f.read()
    lines = text.splitlines()
    regions = parse_memory_config(lines)

    try:
        start = next(i for i, l in enumerate(lines)
                     if l.startswith('Linker script and memory map'))
    except StopIteration:
        raise SystemExit('map-файл без секции "Linker script and memory map"')

    entries = []
    out_sec = ''
    pending_sec = None      # имя входной секции с длинным названием (перенос)
    out_pending = False     # заголовок выходной секции ждёт строку addr+size
    sec_sizes = {}          # out_sec -> (addr, size) из собственной строки
    sec_has_in = set()      # out_sec, у которых есть входные секции/fill

    def norm_obj(obj):
        obj = obj.strip()
        obj = re.sub(r'^(\./)+', '', obj.replace('\\', '/'))
        return obj

    def module_of(obj):
        m = re.search(r'([\w.+-]+\.a)\(', obj)
        if m:
            return m.group(1)[:-2]              # libc.a(lib_a-memcpy.o) -> "libc"
        if 'AZURE_RTOS' in obj:
            return 'AzureRTOS'
        if 'Drivers' in obj:
            return 'Drivers'
        m = re.search(r'VectorLib/(?:Src|Inc)/(?:([\w.-]+)/)?', obj)
        if m:
            return 'VectorLib/' + (m.group(1) if m.group(1) else 'Src')
        if 'VectorLib/' in obj:
            return 'VectorLib'
        if 'Core/Src' in obj:
            return 'Core/Src'
        if obj.startswith('startup_') or '/startup_' in obj:
            return 'startup'
        if obj.startswith('('):
            return '(служебное)'                # fill/резерв кучи-стека линкера
        d = obj.rsplit('/', 1)[0] if '/' in obj else ''
        return d if d else 'other'

    def add(in_sec, addr, size, obj):
        kind_area = classify_addr(addr, regions)
        base = out_sec.split('.')[0] if out_sec.startswith('.') else out_sec
        name = in_sec or out_sec
        if kind_area == 'FLASH':
            kind = 'flash'
        elif kind_area == 'RAM':
            # .data в RAM занимает и flash (LMA-копия загружается стартапом)
            kind = 'flash+ram' if name.startswith('.data') else 'ram'
        else:
            kind = 'flash' if base in ('.text', '.rodata', '.isr_vector') else 'ram'
        entries.append(dict(obj=obj, module=module_of(obj), out_sec=out_sec,
                            in_sec=name, addr=addr, size=size, kind=kind))

    i = start + 1
    while i < len(lines):
        l = lines[i]
        i += 1
        if not l.strip():
            continue
        # конец карты: отброшенные секции и кросс-таблица не считаются
        if l.startswith('Discarded input sections') or l.startswith('Cross Reference Table'):
            break
        # выходная секция: имя в колонке 0 (addr+size — на следующей строке)
        m = re.match(r'^(\.\S+|COMMON)\b\s*$', l)
        if m:
            out_sec = m.group(1)
            out_pending = True
            continue
        # выходная секция одной строкой: ".text  0xADDR  0xSIZE [0xLMA]"
        m = re.match(r'^(\.\S+|COMMON)\s+(%s)\s+(%s)(?:\s+%s)?\s*$' % (HEX, HEX, HEX), l)
        if m:
            out_sec = m.group(1)
            sec_sizes[out_sec] = (int(m.group(2), 16), int(m.group(3), 16))
            out_pending = False
            continue
        ls = l.rstrip()
        # *fill*: байты выравнивания — в модуль "(fill)"
        m = re.match(r'^\s+\*fill\*\s+(%s)\s+(%s)\s*$' % (HEX, HEX), ls)
        if m:
            out_pending = False
            sec_has_in.add(out_sec)
            add('.fill', int(m.group(1), 16), int(m.group(2), 16), '(fill: выравнивание)')
            continue
        # входная секция одной строкой: " .text.Foo  0xADDR  0xSIZE  obj"
        m = re.match(r'^\s(\.\S+|COMMON)\s+(%s)\s+(%s)\s+(\S.*)$' % (HEX, HEX), ls)
        if m:
            out_pending = False
            sec_has_in.add(out_sec)
            add(m.group(1), int(m.group(2), 16), int(m.group(3), 16), norm_obj(m.group(4)))
            pending_sec = None
            continue
        # длинное имя входной секции: строка только с именем, данные на след.
        m = re.match(r'^\s(\.\S+)$', ls)
        if m:
            pending_sec = m.group(1)
            continue
        # продолжение: "                0xADDR  0xSIZE  obj" (obj может отсутствовать
        # у секций linker-скрипта, напр. ._user_heap_stack)
        m = re.match(r'^\s+(%s)\s+(%s)(?:\s+(\S.*))?$' % (HEX, HEX), ls)
        if m:
            addr = int(m.group(1), 16)
            size = int(m.group(2), 16)
            if (m.group(3) is None) and out_pending:
                # перенесённая строка САМОЙ выходной секции (addr+size без
                # объекта): запоминаем; посчитаем только если у секции нет
                # входных (резерв linker-скрипта), иначе был бы двойной счёт
                sec_sizes[out_sec] = (addr, size)
                out_pending = False
                pending_sec = None
                continue
            out_pending = False
            sec_has_in.add(out_sec)
            if size:
                obj = norm_obj(m.group(3)) if m.group(3) else '(linker script)'
                add(pending_sec or out_sec, addr, size, obj)
            pending_sec = None
            continue
        # прочее (LOAD, PROVIDE, символы) — игнор

    # Выходные секции БЕЗ входных (резерв linker-скрипта: ._user_heap_stack и
    # т.п.) считаем по их собственной строке addr+size; у секций со входами
    # собственная строка игнорируется (иначе двойной счёт).
    for sec, (addr, size) in sec_sizes.items():
        if size and sec not in sec_has_in:
            area = classify_addr(addr, regions)
            kind = 'flash' if area == 'FLASH' else 'ram'
            entries.append(dict(obj='(linker script)', module='(служебное)',
                                out_sec=sec, in_sec=sec, addr=addr,
                                size=size, kind=kind))
    return entries, regions


# ---------------------------------------------------------------------------
# Отчёт
# ---------------------------------------------------------------------------

def human(n):
    if n >= 1024 * 1024:
        return '%.2f MiB' % (n / (1024.0 * 1024.0))
    if n >= 1024:
        return '%.1f KiB' % (n / 1024.0)
    return '%d B' % n


def build_report(entries, regions, top):
    flash_by_mod, ram_by_mod = {}, {}
    flash_by_obj, ram_by_obj = {}, {}
    ram_syms = []
    tot_flash = tot_ram = tot_data = 0

    for e in entries:
        f = e['size'] if e['kind'] in ('flash', 'flash+ram') else 0
        r = e['size'] if e['kind'] in ('ram', 'flash+ram') else 0
        flash_by_mod[e['module']] = flash_by_mod.get(e['module'], 0) + f
        ram_by_mod[e['module']] = ram_by_mod.get(e['module'], 0) + r
        flash_by_obj[e['obj']] = flash_by_obj.get(e['obj'], 0) + f
        ram_by_obj[e['obj']] = ram_by_obj.get(e['obj'], 0) + r
        if r and e['in_sec'].startswith(('.bss', '.data', 'COMMON')):
            ram_syms.append((r, e['in_sec'], e['obj']))
        tot_flash += f
        tot_ram += r
        if e['kind'] == 'flash+ram':
            tot_data += e['size']

    out = []
    out.append('# Отчёт памяти (по linker map)')
    out.append('')
    out.append('FLASH = .isr_vector+.text+.rodata+.ARM.ex* + LMA-копия .data; '
               'RAM = .data+.bss+COMMON+резерв кучи/стека.')
    out.append('')
    # регионы: занятость по каждому (VMA секций + LMA-копия .data в FLASH)
    if regions:
        out.append('## Регионы (Memory Configuration)')
        out.append('')
        out.append('| Регион | Начало | Длина | Занято | % |')
        out.append('|---|---|---|---|---|')
        flash_regs = sorted([(n, o, l) for n, (o, l) in regions.items() if o < 0x20000000],
                            key=lambda x: x[1])
        ram_regs = sorted([(n, o, l) for n, (o, l) in regions.items() if o >= 0x20000000],
                          key=lambda x: x[1])
        f_len = sum(l for _, _, l in flash_regs) or 1
        r_len = sum(l for _, _, l in ram_regs) or 1
        reg_used = {n: 0 for n, _, _ in flash_regs + ram_regs}
        for e in entries:
            for n, o, l in flash_regs + ram_regs:
                if o <= e['addr'] < o + l:
                    if e['kind'] in ('ram', 'flash+ram'):
                        reg_used[n] += e['size']
                    elif e['kind'] == 'flash':
                        reg_used[n] += e['size']
                    break
        if flash_regs:                      # LMA-копия .data лежит во FLASH
            reg_used[flash_regs[0][0]] += tot_data
        for n, o, l in flash_regs + ram_regs:
            out.append('| %s | 0x%08X | %s | %s | %.1f%% |'
                       % (n, o, human(l), human(reg_used[n]), 100.0 * reg_used[n] / l))
        out.append('')
        out.append('Всего занято: **FLASH %s** (%.1f%% от %s), **RAM %s** '
                   '(статика; %.1f%% от %s), из них .data = %s (и там, и там).'
                   % (human(tot_flash), 100.0 * tot_flash / f_len, human(f_len),
                      human(tot_ram), 100.0 * tot_ram / r_len, human(r_len),
                      human(tot_data)))
    else:
        out.append('Всего занято: FLASH %s, RAM %s (.data = %s)'
                   % (human(tot_flash), human(tot_ram), human(tot_data)))
    out.append('')

    def table(title, flash_d, ram_d, key_hdr):
        rows = sorted(set(list(flash_d) + list(ram_d)),
                      key=lambda k: (ram_d.get(k, 0), flash_d.get(k, 0)), reverse=True)
        out.append('## %s' % title)
        out.append('')
        out.append('| %s | FLASH | RAM |' % key_hdr)
        out.append('|---|---|---|')
        for k in rows[:top]:
            f, r = flash_d.get(k, 0), ram_d.get(k, 0)
            if f or r:
                out.append('| %s | %s | %s |' % (k, human(f) if f else '—',
                                                 human(r) if r else '—'))
        if len(rows) > top:
            rest_f = sum(flash_d.get(k, 0) for k in rows[top:])
            rest_r = sum(ram_d.get(k, 0) for k in rows[top:])
            out.append('| _остальные (%d)_ | %s | %s |' % (len(rows) - top,
                       human(rest_f), human(rest_r)))
        out.append('')

    table('По модулям', flash_by_mod, ram_by_mod, 'Модуль')
    table('По object-файлам (детально)', flash_by_obj, ram_by_obj, 'Объект')

    out.append('## Топ-%d символов/секций RAM (кто именно кушает)' % top)
    out.append('')
    out.append('| RAM | Секция (имя = переменная при -fdata-sections) | Объект |')
    out.append('|---|---|---|')
    for size, sec, obj in sorted(ram_syms, reverse=True)[:top]:
        out.append('| %s | %s | %s |' % (human(size), sec, obj))
    out.append('')
    return '\n'.join(out), (flash_by_obj, ram_by_obj)


# ---------------------------------------------------------------------------
# Самопроверка на синтетическом map-файле
# ---------------------------------------------------------------------------

SELFTEST_MAP = """Archive member included to satisfy reference by file (symbol)

Discarded input sections

 .text.unused   0x00000000        0x99 dead_object.o

Memory Configuration

Name             Origin             Length             Attributes
RAM              0x20000000         0x00030000         xrw
RAM2             0x20030000         0x00010000         xrw
FLASH            0x08000000         0x00100000         xr

Linker script and memory map

LOAD c:/st/gcc-arm-none-eabi/lib/libc.a
                0x00000000                PROVIDE (errno, 0)
.isr_vector     0x08000000       0x1ec
 *(.isr_vector)
 .isr_vector    0x08000000       0x1ec startup_stm32u575xx.o
                0x08000000                g_pfnVectors
.text           0x080001ec      0x1000
 .text.Ble_Run
                0x080001ec       0x100 Core/VectorLib/Src/Ble_vector.o
                0x080001ec                Ble_Run
 .text.Lora_Run
                0x080002ec       0x200 Core/VectorLib/Src/Lora_S7678S.o
 *fill*         0x080004ec         0x4
 .text.memcpy   0x080004f0        0x40 c:/st/gcc-arm-none-eabi/lib/libc.a(lib_a-memcpy.o)
                0x080004f0                memcpy
.ARM.exidx
                0x08000530         0x8
 .ARM.exidx      0x08000530         0x8 Core/VectorLib/Src/Ble_vector.o
.rodata         0x08000538        0x40
 .rodata.str    0x08000530        0x40 Core/VectorLib/Src/Common/Src/vector_log.o
.data           0x20000000        0x80     0x08000570
 .data.cfg      0x20000000        0x80 Core/VectorLib/Src/Config_save_read.o
                0x20000000                g_cfg
.bss            0x20000080       0x800
 .bss.ble_task_stack
                0x20000080       0x800 Core/VectorLib/Src/Vector_main.o
                0x20000080                ble_task_stack
COMMON          0x20000880        0x10
 .bss.shared    0x20000880        0x10 Core/Src/main.o
                0x20000880                shared_var
._user_heap_stack
                0x20000890       0x600
                0x20000890                _Min_Heap_Size = 0x200

Cross Reference Table

Symbol                                     File
"""


def selftest():
    entries, regions = parse_map(SELFTEST_MAP, is_text=True)
    assert len(regions) == 3, regions
    f = sum(e['size'] for e in entries if e['kind'] in ('flash', 'flash+ram'))
    r = sum(e['size'] for e in entries if e['kind'] in ('ram', 'flash+ram'))
    # FLASH: isr 0x1ec + text 0x100+0x200+4+0x40 + exidx 0x8 (перенесённая
    # summary-строка НЕ считается дважды) + rodata 0x40 + .data LMA 0x80
    assert f == 0x1ec + 0x100 + 0x200 + 4 + 0x40 + 0x8 + 0x40 + 0x80, hex(f)
    # RAM: .data 0x80 + bss 0x800 + COMMON 0x10 + heap_stack 0x600
    assert r == 0x80 + 0x800 + 0x10 + 0x600, hex(r)
    mods = {}
    for e in entries:
        mods[e['module']] = mods.get(e['module'], 0) + e['size']
    assert mods.get('libc') == 0x40, mods            # архив libc.a(lib_a-memcpy.o)
    assert mods.get('startup') == 0x1ec, mods        # startup_stm32u575xx.o
    assert 'VectorLib/Src' in mods, mods             # Core/VectorLib/Src/*.o
    assert mods.get('(служебное)') == 0x600 + 4, mods  # heap_stack + fill
    assert '(linker script)' in [e['obj'] for e in entries]   # ._user_heap_stack
    assert not any('dead_object' in e['obj'] for e in entries)  # Discarded не считаем
    rep, _ = build_report(entries, regions, 10)
    assert 'Топ' in rep and 'FLASH' in rep
    print('SELFTEST OK: flash=%s ram=%s modules=%d' % (hex(f), hex(r), len(mods)))


# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description='RAM/FLASH по модулям из .map GNU ld')
    ap.add_argument('mapfile', nargs='?', help='путь к .map (Debug/*.map)')
    ap.add_argument('--top', type=int, default=25, help='строк в таблицах (25)')
    ap.add_argument('--csv', metavar='FILE', help='выгрузить пообъектную таблицу в CSV')
    ap.add_argument('--selftest', action='store_true', help='самопроверка парсера')
    a = ap.parse_args()

    if a.selftest:
        selftest()
        return
    if not a.mapfile:
        ap.error('нужен путь к .map файлу (или --selftest)')

    entries, regions = parse_map(a.mapfile)
    rep, (fobj, robj) = build_report(entries, regions, a.top)
    print(rep)
    if a.csv:
        with open(a.csv, 'w', newline='', encoding='utf-8') as fh:
            w = csv.writer(fh)
            w.writerow(['object', 'flash_bytes', 'ram_bytes'])
            for k in sorted(set(list(fobj) + list(robj))):
                w.writerow([k, fobj.get(k, 0), robj.get(k, 0)])
        print('CSV: %s' % a.csv, file=sys.stderr)


if __name__ == '__main__':
    main()
