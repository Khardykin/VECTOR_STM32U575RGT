#!/usr/bin/env python3
"""
apply_p1.py — Патч 1: Command_vector / Modbus / tools / docs
Запуск из корня проекта:  py apply_p1.py

Все оригиналы бэкапятся в _backup_p1/.
Если паттерн не найден — скрипт печатает [WARN] и продолжает.
"""
import os, sys, re, shutil
from pathlib import Path

ROOT = Path(".")
BAK = ROOT / "_backup_p1"
REPORT = []

def log(msg):
    REPORT.append(msg)
    print(msg)

def backup(p: Path):
    rel = p.relative_to(ROOT)
    dst = BAK / rel
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(p, dst)

def read(p: Path) -> str:
    return p.read_text(encoding="utf-8", errors="replace")

def write(p: Path, s: str):
    p.write_text(s, encoding="utf-8")

def apply_str(src, old, new, label, count=1):
    if old in src:
        src = src.replace(old, new, count)
        log(f"  [OK] {label}")
    else:
        log(f"  [WARN] {label}: не найден фрагмент")
    return src

def apply_re(src, pattern, repl, label, flags=0):
    s2, n = re.subn(pattern, repl, src, count=1, flags=flags)
    if n:
        log(f"  [OK] {label}")
        return s2
    log(f"  [WARN] {label}: regex не сработал")
    return src


# ═══════════════════════════════════════════════════════════════════════════
# 1. Command_vector.c
# ═══════════════════════════════════════════════════════════════════════════
def patch_command_vector():
    p = ROOT / "Core" / "VectorLib" / "Src" / "Command_vector.c"
    if not p.exists():
        log(f"[SKIP] {p}"); return
    backup(p)
    src = read(p)

    # ── 1a. Удаляем отладочный счётчик ──────────────────────────────────────
    src = apply_re(src,
        r'\bvolatile\s+uint32_t\s+cv_dbg_cmd20_calls\s*=\s*0\s*;\s*\n?',
        '', 'удаление объявления cv_dbg_cmd20_calls')
    src = apply_re(src,
        r'\bcv_dbg_cmd20_calls\s*\+\+\s*;\s*\n?',
        '', 'удаление инкремента cv_dbg_cmd20_calls')
    src = apply_re(src,
        r'\bcv_dbg_cmd20_calls\s*=\s*0\s*;\s*\n?',
        '', 'удаление сброса cv_dbg_cmd20_calls')

    # ── 1b. Неизвестные команды: игнорируем, не отвечаем ────────────────────
    # Вариант 1: default в switch возвращает CmdError
    src = apply_re(src,
        r'(default\s*:)\s*\n(\s*)return\s+CmdError\s*\(\s*out\s*,\s*mas\s*,\s*CMD_ERR_ILLEGAL_FUNCTION\s*\)\s*;',
        r'\1\n\2/* Неизвестная функция: игнорируем, не отвечаем */\n\2return 0u;',
        'неизвестная функция → игнор (вариант switch)')
    # Вариант 2: if-else цепочка
    src = apply_re(src,
        r'else\s*\{\s*\n(\s*)return\s+CmdError\s*\(\s*out\s*,\s*mas\s*,\s*CMD_ERR_ILLEGAL_FUNCTION\s*\)\s*;\s*\n(\s*)\}',
        r'else {\n\1/* Неизвестная функция: игнорируем, не отвечаем */\n\1return 0u;\n\2}',
        'неизвестная функция → игнор (вариант else)')

    # ── 1c. Broadcast (адрес 0) отвечает ────────────────────────────────────
    # Убираем ранний return для адреса 0
    src = apply_re(src,
        r'if\s*\(\s*mas\[0\]\s*==\s*0u?\s*\)\s*\{\s*\n?\s*return\s+0u?\s*;\s*\n?\s*\}',
        '/* broadcast (адрес 0): отвечаем — опрос 1-на-1 без прописки адреса */',
        'broadcast: убран ранний return для адреса 0')
    # Паттерн: если проверка "не наш и не 0" — оставляем, но добавляем коммент
    src = apply_re(src,
        r'(if\s*\(\s*mas\[0\]\s*!=\s*\w+\s*&&\s*mas\[0\]\s*!=\s*0u?\s*\))',
        r'\1  /* broadcast=0 тоже принимаем и отвечаем */',
        'broadcast: добавлен комментарий')

    # ── 1d. LOG_RING_SLOTS ────────────────────────────────────────────────────
    # Заменяем хардкод маски 0x7FFF на LOG_RING_SLOTS - 1
    src = apply_re(src,
        r'&\s*0x7FFF\b',
        '& (LOG_RING_SLOTS - 1u)',
        'маска 0x7FFF → (LOG_RING_SLOTS - 1u)')
    src = apply_re(src,
        r'%\s*0x8000\b',
        '% LOG_RING_SLOTS',
        'модуль 0x8000 → LOG_RING_SLOTS')
    src = apply_re(src,
        r'&\s*\(?\s*LOG_RING_SLOTS\s*-\s*1\s*\)?',
        '& (LOG_RING_SLOTS - 1u)',
        'нормализация маски LOG_RING_SLOTS')

    # Если дефайна нет — добавляем после последнего #include
    if '#define LOG_RING_SLOTS' not in src:
        # Находим последний #include и вставляем после него
        m = list(re.finditer(r'#include\s+[<"][^>"]+[>"]\s*\n', src))
        if m:
            pos = m[-1].end()
            insert = ('\n/* Размер кольцевого буфера логов (записей).\n'
                      '   Маска для индекса: (LOG_RING_SLOTS - 1). */\n'
                      '#ifndef LOG_RING_SLOTS\n'
                      '#define LOG_RING_SLOTS  32768u\n'
                      '#endif\n')
            src = src[:pos] + insert + src[pos:]
            log("  [OK] добавлен #define LOG_RING_SLOTS")
        else:
            log("  [WARN] не нашёл место для #define LOG_RING_SLOTS")

    # ── 1e. sys_state.process: ставим ПОСЛЕ проверок ────────────────────────
    # Ищем паттерн: присваивание process ПЕРЕД if-проверкой с return
    # Переносим присваивание после блока проверок
    # Паттерн: "sys_state.process = X;\n  if (...) { ... return ...; }"
    src = apply_re(src,
        r'(\s*)(sys_state\.process\s*=\s*\w+\s*;)\s*\n'
        r'(\s*)(if\s*\([^)]+\)\s*\{[^}]*return[^}]*\})',
        r'\1/* process ставим после проверок */\1\4\1\2',
        'sys_state.process перенесён после проверок (вариант 1)',
        flags=re.DOTALL)

    # ── 1f. 0x0E (CMD_OP_WRITE_MCU): 6-байтное эхо ─────────────────────────
    # Ищем "int_write_pending = 1;" за которым следует "return 0u;"
    src = apply_re(src,
        r'(cmd_state\.int_write_pending\s*=\s*1u?\s*;)\s*\n(\s*)return\s+0u?\s*;',
        r'\1\n'
        r'\2/* 6-байтное эхо: мастер подтверждает приём */\n'
        r'\2if (cap >= 6u) {\n'
        r'\2  out[0]=mas[0]; out[1]=mas[1]; out[2]=mas[2];\n'
        r'\2  out[3]=mas[3]; out[4]=mas[4]; out[5]=mas[5];\n'
        r'\2  return 6u;\n'
        r'\2}\n'
        r'\2return 0u;',
        '0x0E: ответ 6-байтным эхо')

    # ── 1g. 0x0F (CMD_OP_WRITE_EXT): 6-байтное эхо ─────────────────────────
    src = apply_re(src,
        r'(cmd_state\.ext_write_pending\s*=\s*1u?\s*;)\s*\n(\s*)return\s+0u?\s*;',
        r'\1\n'
        r'\2/* 6-байтное эхо: мастер подтверждает приём */\n'
        r'\2if (cap >= 6u) {\n'
        r'\2  out[0]=mas[0]; out[1]=mas[1]; out[2]=mas[2];\n'
        r'\2  out[3]=mas[3]; out[4]=mas[4]; out[5]=mas[5];\n'
        r'\2  return 6u;\n'
        r'\2}\n'
        r'\2return 0u;',
        '0x0F: ответ 6-байтным эхо')

    write(p, src)
    log(f"  [DONE] {p}")


# ═══════════════════════════════════════════════════════════════════════════
# 2. tools/send_frames.py
# ═══════════════════════════════════════════════════════════════════════════
def patch_send_frames():
    p = ROOT / "tools" / "send_frames.py"
    if not p.exists():
        log(f"  [SKIP] {p}"); return
    backup(p)
    src = read(p)

    # ── 2a. Кадр 0x0C: было 8 байт → стало 10 байт ─────────────────────────
    # Старый: rd(0x0C, 0x0000, 4, slave)  →  struct.pack(">BBHH", slave, 0x0C, 0, 4)
    # Новый:  [slave, 0x0C, 0x00,0x00,0x00,0x00, 0x00,0x04] + CRC  (10 байт)
    
    # Паттерн 1: вызов rd(0x0C, ...)
    src = apply_re(src,
        r'rd\(0x0C,\s*0x0000,\s*4,\s*slave\)',
        r'make_frame_log_read(slave)',
        '0x0C: замена rd() на make_frame_log_read()')

    # Паттерн 2: hex-строка "64 0C 00 00 00 04"
    src = apply_re(src,
        r'"64\s+0C\s+00\s+00\s+00\s+04"',
        '"64 0C 00 00 00 00 00 04"',
        '0x0C: hex-строка → 10 байт')

    # Паттерн 3: bytes([slave, 0x0C, ...]) с 6 байтами данных
    src = apply_re(src,
        r'bytes\(\[slave,\s*0x0C,\s*0x00,\s*0x00,\s*0x00,\s*0x04\]\)',
        'bytes([slave, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04])',
        '0x0C: bytes() → 10 байт')

    # Добавляем helper-функцию make_frame_log_read если её нет
    if 'make_frame_log_read' not in src:
        helper = textwrap.dedent('''\

def make_frame_log_read(slave, ptr=0, cnt=4):
    """Кадр 0x0C: 10 байт = [slave, 0x0C, ptr(4B BE), cnt(2B BE)] + CRC."""
    import struct
    data = struct.pack(">BB I H", slave, 0x0C, ptr, cnt)
    # struct ">BB I H" = 1+1+4+2 = 8 байт данных
    crc = modbus_crc(data)
    return data + struct.pack("<H", crc)

''')
        # Вставляем перед def make_cases или перед первым def
        if 'def make_cases' in src:
            src = src.replace('def make_cases', helper + 'def make_cases', 1)
            log("  [OK] добавлена make_frame_log_read()")
        elif 'def xfer' in src:
            src = src.replace('def xfer', helper + 'def xfer', 1)
            log("  [OK] добавлена make_frame_log_read() перед xfer")
        else:
            log("  [WARN] не нашёл место для вставки make_frame_log_read")

    # ── 2b. Парсер ответа 0x0C: ждём cnt*40 ─────────────────────────────────
    # Ищем проверку длины ответа для 0x0C и меняем ожидание
    # Паттерн: "1 + cnt * 2" или "3 + cnt * 2 + 2" в контексте 0x0C
    src = apply_re(src,
        r'(0x0C[^;]*?)(\d+\s*\+\s*cnt\s*\*\s*2)',
        r'\g<1>3 + cnt * 40 + 2',
        '0x0C парсер: ожидание cnt*40')
    
    # Паттерн: если есть явная проверка "expect" для log read
    src = apply_re(src,
        r'("log read[^"]*".*?)expect[^\n]*',
        r'\1expect: 3 + cnt*40 + 2 bytes (log entries × 40)',
        '0x0C: обновлён комментарий expect',
        flags=re.DOTALL)

    write(p, src)
    log(f"  [DONE] {p}")


# ═══════════════════════════════════════════════════════════════════════════
# 3. tools/verify_registers.py
# ═══════════════════════════════════════════════════════════════════════════
def patch_verify_registers():
    p = ROOT / "tools" / "verify_registers.py"
    if not p.exists():
        log(f"  [SKIP] {p}"); return
    backup(p)
    src = read(p)

    # ── 3a. Кадр 0x0C: 10 байт ──────────────────────────────────────────────
    src = apply_re(src,
        r'rd\(0x0C,\s*0x0000,\s*4,\s*slave\)',
        r'make_frame_log_read(slave)',
        '0x0C: замена rd()')
    src = apply_re(src,
        r'"64\s+0C\s+00\s+00\s+00\s+04"',
        '"64 0C 00 00 00 00 00 04"',
        '0x0C: hex → 10 байт')
    src = apply_re(src,
        r'bytes\(\[slave,\s*0x0C,\s*0x00,\s*0x00,\s*0x00,\s*0x04\]\)',
        'bytes([slave, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04])',
        '0x0C: bytes() → 10 байт')

    # Добавляем helper если нет
    if 'make_frame_log_read' not in src:
        helper = textwrap.dedent('''\

def make_frame_log_read(slave, ptr=0, cnt=4):
    """Кадр 0x0C: 10 байт."""
    import struct
    data = struct.pack(">BB I H", slave, 0x0C, ptr, cnt)
    crc = modbus_crc(data) if 'modbus_crc' in dir() else crc16(data)
    return data + struct.pack("<H", crc)

''')
        # Вставляем после импортов
        m = re.search(r'(import\s+struct\s*\n)', src)
        if m:
            src = src[:m.end()] + helper + src[m.end():]
            log("  [OK] добавлена make_frame_log_read()")
        else:
            m2 = re.search(r'(import\s+serial\s*\n)', src)
            if m2:
                src = src[:m2.end()] + helper + src[m2.end():]
                log("  [OK] добавлена make_frame_log_read()")

    # ── 3b. Парсер: ждём cnt*40 ─────────────────────────────────────────────
    src = apply_re(src,
        r'(0x0C[^;]*?)(\d+\s*\+\s*cnt\s*\*\s*2)',
        r'\g<1>3 + cnt * 40 + 2',
        '0x0C парсер: ожидание cnt*40')

    # ── 3c. SAFE_WRITE: расширяем ────────────────────────────────────────────
    new_safe = textwrap.dedent('''\
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
    }''')

    if 'SAFE_WRITE' in src:
        src = apply_re(src,
            r'SAFE_WRITE\s*=\s*\{[^}]*\}',
            new_safe,
            'SAFE_WRITE: заменён на расширенный')
    else:
        # Вставляем после последнего import
        m = list(re.finditer(r'^(import|from)\s+.*$', src, re.MULTILINE))
        if m:
            pos = m[-1].end()
            src = src[:pos] + '\n\n' + new_safe + '\n' + src[pos:]
            log("  [OK] SAFE_WRITE: добавлен")

    write(p, src)
    log(f"  [DONE] {p}")


# ═══════════════════════════════════════════════════════════════════════════
# 4. tools/retry_frames.py
# ═══════════════════════════════════════════════════════════════════════════
def patch_retry_frames():
    p = ROOT / "tools" / "retry_frames.py"
    if not p.exists():
        log(f"  [SKIP] {p}"); return
    backup(p)
    src = read(p)

    src = apply_re(src,
        r'"64\s+0C\s+00\s+00\s+00\s+04"',
        '"64 0C 00 00 00 00 00 04"',
        '0x0C: hex → 10 байт')
    src = apply_re(src,
        r'rd\(0x0C,\s*0x0000,\s*4,\s*slave\)',
        r'make_frame_log_read(slave)',
        '0x0C: замена rd()')
    src = apply_re(src,
        r'bytes\(\[slave,\s*0x0C,\s*0x00,\s*0x00,\s*0x00,\s*0x04\]\)',
        'bytes([slave, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04])',
        '0x0C: bytes() → 10 байт')

    # Добавляем helper если используется make_frame_log_read но не определена
    if 'make_frame_log_read' in src and 'def make_frame_log_read' not in src:
        helper = textwrap.dedent('''\

def make_frame_log_read(slave, ptr=0, cnt=4):
    """Кадр 0x0C: 10 байт."""
    import struct
    data = struct.pack(">BB I H", slave, 0x0C, ptr, cnt)
    crc = crc16(data)
    return data + struct.pack("<H", crc)

''')
        # Вставляем перед первым использованием
        idx = src.find('make_frame_log_read(')
        # Находим начало строки
        line_start = src.rfind('\n', 0, idx) + 1
        src = src[:line_start] + helper + src[line_start:]
        log("  [OK] добавлена make_frame_log_read()")

    write(p, src)
    log(f"  [DONE] {p}")


# ═══════════════════════════════════════════════════════════════════════════
# 5. Документация
# ═══════════════════════════════════════════════════════════════════════════
DOC_BLOCK = """
---

## Форматы команд 0x0C / 0x0E / 0x0F (Patch 1)

### 0x0C — Чтение журнала (Read Log)

**Запрос: 10 байт**

| Байт | Поле | Значение |
|------|------|----------|
| 0 | Slave | адрес (0 = broadcast) |
| 1 | Function | `0x0C` |
| 2–5 | Pointer | `uint32_t BE`, указатель начала (0 = с начала) |
| 6–7 | Count | `uint16_t BE`, количество записей |
| 8–9 | CRC | CRC-16 Modbus (Lo, Hi) |

Пример: `64 0C 00 00 00 00 00 04 CRC_Lo CRC_Hi` — прочитать 4 записи с начала.

**Ответ:** `[Slave, 0x0C, Byte_Count, Data(cnt×40), CRC_Lo, CRC_Hi]`

Парсер хоста ждёт `3 + cnt*40 + 2` байт (каждая запись лога = 40 байт).

### 0x0E — Запись во внутреннюю Flash (MCU)

Staging-режим: данные копируются в `cmd_state.write_buf`,
выставляется `int_write_pending = 1`. Фактическая запись выполняется
асинхронно в Measure Task через `Command_vector_ExecutePendingActions()`.

**Ответ: 6-байтное эхо** `[Slave, 0x0E, addr_hi, addr_lo, len_hi, len_lo]`
Мастер по эхо подтверждает приём пакета.

### 0x0F — Запись во внешнюю Flash (SPI NOR)

Аналогично 0x0E, но `ext_write_pending = 1`.

**Ответ: 6-байтное эхо.**

### Broadcast (адрес 0x00)

Контроллер **отвечает** на broadcast-запросы (адрес 0).
Используется для опроса прибора «1-на-1» без прописки адреса на линии.

### Указатель кольцевого буфера логов

* Указатель — монотонный `uint32_t` (непрерывно растёт).
* Размер кольца: `LOG_RING_SLOTS` (по умолчанию 32768 записей).
* Физический индекс: `monotonic_ptr & (LOG_RING_SLOTS - 1)`.
* Каждая запись лога: 40 байт.

### Неизвестные команды

Неизвестный код функции **игнорируется** — ответ не формируется.
Мастер получает таймаут. Это упрощает диагностику и не засоряет шину.
"""

def patch_docs():
    for name in ["docs/Command.md", "docs/ARCHITECTURE.md"]:
        p = ROOT / name
        if not p.exists():
            log(f"  [SKIP] {p}"); continue
        backup(p)
        src = read(p)
        if "## Форматы команд 0x0C / 0x0E / 0x0F" not in src:
            src += "\n" + DOC_BLOCK
            write(p, src)
            log(f"  [OK] {p}: добавлено описание")
        else:
            log(f"  [ALREADY] {p}")


# ═══════════════════════════════════════════════════════════════════════════
# MAIN
# ═══════════════════════════════════════════════════════════════════════════
import textwrap

def main():
    print("=" * 72)
    print("  PATCH 1: Command_vector / Modbus / tools / docs")
    print(f"  Backup: {BAK.resolve()}")
    print("=" * 72)
    print()

    patch_command_vector()
    print()
    patch_send_frames()
    print()
    patch_verify_registers()
    print()
    patch_retry_frames()
    print()
    patch_docs()

    print()
    print("=" * 72)
    print("  ИТОГ:")
    warns = [r for r in REPORT if "[WARN]" in r]
    oks = [r for r in REPORT if "[OK]" in r]
    print(f"  Применено: {len(oks)}, Предупреждений: {len(warns)}")
    if warns:
        print("\n  Предупреждения (нужно проверить вручную):")
        for w in warns:
            print(f"    {w}")
    print()
    print("  Дальше:")
    print("    1. Пересобери прошивку в STM32CubeIDE")
    print("    2. py tools/send_frames.py COM4")
    print("    3. py tools/verify_registers.py COM4 --slave 100 --write")
    print("    4. py tools/retry_frames.py COM4")
    print("=" * 72)

if __name__ == "__main__":
    main()