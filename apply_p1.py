#!/usr/bin/env python3
"""
apply_p1.py — Патч 1: Command_vector / Modbus fixes
Запуск:  py apply_p1.py   (из корня проекта, где лежат Core/, tools/, docs/)

Что делает:
  1. Command_vector.c:
     - удаляет отладочный volatile cv_dbg_cmd20_calls
     - sys_state.process ставит ПОСЛЕ проверок
     - broadcast (адрес 0) отвечает
     - лог-кольцо использует LOG_RING_SLOTS (маска)
     - неизвестные команды игнорируются (без exception)
     - 0x0E/0x0F отвечают 6-байтным эхо
  2. tools/send_frames.py:
     - кадр 0x0C → 10 байт
     - парсер ответа ждёт cnt*40
  3. tools/verify_registers.py:
     - кадр 0x0C → 10 байт
     - парсер ответа ждёт cnt*40
     - SAFE_WRITE расширен
  4. tools/retry_frames.py:
     - кадр 0x0C → 10 байт (если есть)
  5. docs/Command.md, docs/ARCHITECTURE.md — дописаны форматы
"""

import os, sys, re, shutil, textwrap
from pathlib import Path
from datetime import datetime

# ──────────────────────────────────────────────────────────────────────────────
ROOT = Path(".")
BACKUP_DIR = ROOT / "_backup_p1"

def backup(path: Path):
    BACKUP_DIR.mkdir(exist_ok=True)
    rel = path.relative_to(ROOT)
    dst = BACKUP_DIR / rel
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(path, dst)

def patch_file(path: Path, replacements: list, label: str):
    """replacements: list of (old_str, new_str) or (regex_pattern, new_str, flags)"""
    if not path.exists():
        print(f"  [SKIP] {path} не найден")
        return
    backup(path)
    src = path.read_text(encoding="utf-8", errors="replace")
    original = src
    applied = 0
    for item in replacements:
        if len(item) == 2:
            old, new = item
            if old in src:
                src = src.replace(old, new, 1)
                applied += 1
            else:
                print(f"  [WARN] {label}: фрагмент не найден: {old[:80]!r}...")
        elif len(item) == 3:
            pattern, new, flags = item
            src2, n = re.subn(pattern, new, src, count=1, flags=flags)
            if n:
                src = src2
                applied += 1
            else:
                print(f"  [WARN] {label}: regex не сработал: {pattern[:60]!r}")
    if src != original:
        path.write_text(src, encoding="utf-8")
        print(f"  [OK] {path}: {applied} замен")
    else:
        print(f"  [NO-CHANGE] {path}")

# ══════════════════════════════════════════════════════════════════════════════
# 1. Command_vector.c
# ══════════════════════════════════════════════════════════════════════════════
def patch_command_vector():
    p = ROOT / "Core" / "VectorLib" / "Src" / "Command_vector.c"
    if not p.exists():
        print(f"[ERROR] {p} не найден"); return

    src = p.read_text(encoding="utf-8", errors="replace")
    backup(p)
    original = src

    # ── 1a. Удаляем отладочный счётчик cv_dbg_cmd20_calls ──
    src = re.sub(r'\bvolatile\s+uint32_t\s+cv_dbg_cmd20_calls\s*=\s*0\s*;\s*\n?', '', src)
    src = re.sub(r'\bcv_dbg_cmd20_calls\s*\+\+\s*;\s*\n?', '', src)
    src = re.sub(r'\bcv_dbg_cmd20_calls\s*=\s*0\s*;\s*\n?', '', src)

    # ── 1b. Неизвестные команды: игнорируем (не отвечаем) ──
    # В функции парсинга: если функция не входит в список поддерживаемых → возвращаем 0
    # Ищем блок, который отвечает exception 01 на неизвестную функцию, и заменяем на игнор
    src = re.sub(
        r'(default\s*:)\s*\n(\s*)(return\s+CmdError\s*\(\s*out\s*,\s*mas\s*,\s*CMD_ERR_ILLEGAL_FUNCTION\s*\)\s*;)',
        r'\1\n\2/* Неизвестная команда: игнорируем, не отвечаем */\n\2return 0u;',
        src,
        flags=re.DOTALL
    )

    # ── 1c. Broadcast (адрес 0) отвечает ──
    # Ищем проверку вида: if (mas[0] != CV_SLAVE_ADDR && mas[0] != 0u) или аналог
    # Заменяем на: если адрес не наш И не 0 → игнор. Если 0 → отвечаем.
    # Паттерн 1: явная проверка на 0 с возвратом
    src = re.sub(
        r'if\s*\(\s*mas\[0\]\s*==\s*0u?\s*\)\s*\{\s*return\s+0u?\s*;\s*\}',
        '/* broadcast (адрес 0): отвечаем — опрос 1-на-1 без прописки адреса */',
        src
    )
    # Паттерн 2: проверка "не наш адрес и не 0"
    src = re.sub(
        r'if\s*\(\s*mas\[0\]\s*!=\s*(CV_SLAVE_ADDR|cmd_cfg\.slave_addr)\s*&&\s*mas\[0\]\s*!=\s*0u?\s*\)',
        r'if (mas[0] != \1 && mas[0] != 0u)  /* broadcast=0 тоже принимаем */',
        src
    )

    # ── 1d. LOG_RING_SLOTS: маска для кольцевого буфера логов ──
    # Ищем места, где используется маска 0x7FFF или 0xFFFF для индекса лога
    # и заменяем на LOG_RING_SLOTS - 1 (или (LOG_RING_SLOTS - 1u))
    src = re.sub(
        r'(&\s*0x7FFF\b)',
        r'& (LOG_RING_SLOTS - 1u)',
        src
    )
    src = re.sub(
        r'%\s*0x8000\b',
        r'% LOG_RING_SLOTS',
        src
    )
    # Если LOG_RING_SLOTS ещё не определён — добавим дефайн после инклюдов
    if '#define LOG_RING_SLOTS' not in src:
        src = re.sub(
            r'(#include\s+"vector_log\.h"\s*\n)',
            r'\1\n/* Размер кольцевого буфера логов (записей). Маска = LOG_RING_SLOTS-1 */\n'
            r'#ifndef LOG_RING_SLOTS\n#define LOG_RING_SLOTS  32768u\n#endif\n\n',
            src,
            count=1
        )

    # ── 1e. sys_state.process ставим ПОСЛЕ проверок ──
    # Ищем паттерн в обработчиках 0x0C/0x0E/0x0F, где process ставится ДО проверок
    # Типичный паттерн:
    #   sys_state.process = PROCESS_xxx;
    #   if (some_check) return CmdError(...);
    # Переносим: сначала проверки, потом присваивание
    # Это сложно сделать универсально, поэтому ищем конкретные паттерны:
    
    # Паттерн для 0x0C (CmdFunc12): process до проверки лога
    src = re.sub(
        r'(sys_state\.process\s*=\s*PROCESS_\w+\s*;)\s*\n(\s*)(if\s*\([^)]*log[^)]*\)[^{]*\{[^}]*return[^}]*\})',
        r'\2/* process ставим после проверок */\2\3\2\1',
        src,
        flags=re.DOTALL | re.IGNORECASE
    )

    # ── 1f. 0x0E/0x0F: ответ 6-байтным эхо ──
    # Ищем обработчики 0x0E (CmdFunc14/CmdFunc15) и 0x0F (CmdFunc15/CmdFunc20)
    # Паттерн: после записи в cmd_state возвращаем 0 или ничего
    # Заменяем на возврат 6-байтного эхо
    
    # Для 0x0E (CMD_OP_WRITE_MCU): ищем "return 0u;" после записи в flash staging
    # Заменяем на формирование эхо
    src = re.sub(
        r'(cmd_state\.int_write_pending\s*=\s*1u?\s*;)\s*\n(\s*)return\s+0u?\s*;',
        r'\1\n\2/* 6-байтное эхо: мастер знает, что пакет принят */\n'
        r'\2if (cap >= 6u) {\n'
        r'\2  out[0]=mas[0]; out[1]=mas[1]; out[2]=mas[2];\n'
        r'\2  out[3]=mas[3]; out[4]=mas[4]; out[5]=mas[5];\n'
        r'\2  return 6u;\n'
        r'\2}\n\2return 0u;',
        src
    )
    # Для 0x0F (CMD_OP_WRITE_EXT): аналогично
    src = re.sub(
        r'(cmd_state\.ext_write_pending\s*=\s*1u?\s*;)\s*\n(\s*)return\s+0u?\s*;',
        r'\1\n\2/* 6-байтное эхо: мастер знает, что пакет принят */\n'
        r'\2if (cap >= 6u) {\n'
        r'\2  out[0]=mas[0]; out[1]=mas[1]; out[2]=mas[2];\n'
        r'\2  out[3]=mas[3]; out[4]=mas[4]; out[5]=mas[5];\n'
        r'\2  return 6u;\n'
        r'\2}\n\2return 0u;',
        src
    )

    if src != original:
        p.write_text(src, encoding="utf-8")
        print(f"  [OK] {p}: патч применён")
    else:
        print(f"  [NO-CHANGE] {p}")

# ══════════════════════════════════════════════════════════════════════════════
# 2. tools/send_frames.py
# ══════════════════════════════════════════════════════════════════════════════
def patch_send_frames():
    p = ROOT / "tools" / "send_frames.py"
    if not p.exists():
        print(f"  [SKIP] {p}"); return

    src = p.read_text(encoding="utf-8", errors="replace")
    backup(p)
    original = src

    # ── Кадр 0x0C: было 8 байт (64 0C 00 00 00 04 + CRC), стало 10 байт ──
    # Старый формат: rd(0x0C, 0x0000, 4, slave) → struct.pack(">BBHH", ...)
    # Новый формат: 64 0C 00 00 00 00 00 04 + CRC
    src = re.sub(
        r'("log read ptr0 x4"[^,]*,\s*)rd\(0x0C,\s*0x0000,\s*4,\s*slave\)',
        r'\1bytes([slave, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04]) + crc16_bytes(bytes([slave, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04]))',
        src
    )
    # Альтернативный паттерн: если кадр задан как hex-строка
    src = re.sub(
        r'"64\s+0C\s+00\s+00\s+00\s+04"',
        '"64 0C 00 00 00 00 00 04"',
        src
    )

    # ── Парсер ответа 0x0C: ждём cnt*40, а не cnt ──
    # Ищем проверку длины ответа для 0x0C
    src = re.sub(
        r'(if\s+func\s*==\s*0x0C[^:]*:.*?)expect[^\n]*cnt\b',
        r'\1expect_len = 3 + cnt * 40 + 2',
        src,
        flags=re.DOTALL
    )
    # Более общий паттерн: замена "1 + cnt * 2" на "1 + cnt * 40" для 0x0C
    src = re.sub(
        r'(0x0C[^;]*?)1\s*\+\s*cnt\s*\*\s*2',
        r'\g<1>1 + cnt * 40',
        src
    )

    # Если есть функция ожидания ответа — добавляем логику для 0x0C
    # Ищем блок где проверяется длина ответа и добавляем условие
    if 'cnt * 40' not in src:
        # Добавляем комментарий-напоминание
        src = src.replace(
            'def xfer(ser, frame',
            '# NOTE: для 0x0C (Read Log) ответ содержит cnt*40 байт данных + 3 заголовок + 2 CRC\ndef xfer(ser, frame'
        )

    if src != original:
        p.write_text(src, encoding="utf-8")
        print(f"  [OK] {p}")
    else:
        print(f"  [NO-CHANGE] {p}")

# ══════════════════════════════════════════════════════════════════════════════
# 3. tools/verify_registers.py
# ══════════════════════════════════════════════════════════════════════════════
def patch_verify_registers():
    p = ROOT / "tools" / "verify_registers.py"
    if not p.exists():
        print(f"  [SKIP] {p}"); return

    src = p.read_text(encoding="utf-8", errors="replace")
    backup(p)
    original = src

    # ── Кадр 0x0C: 10 байт ──
    src = re.sub(
        r'("log read[^"]*"[^,]*,\s*)rd\(0x0C,\s*0x0000,\s*4,\s*slave\)',
        r'\1bytes([slave, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04]) + crc16_bytes(bytes([slave, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04]))',
        src
    )
    src = re.sub(
        r'"64\s+0C\s+00\s+00\s+00\s+04"',
        '"64 0C 00 00 00 00 00 04"',
        src
    )

    # ── Парсер: ждём cnt*40 ──
    src = re.sub(
        r'(0x0C[^;]*?)1\s*\+\s*cnt\s*\*\s*2',
        r'\g<1>1 + cnt * 40',
        src
    )

    # ── SAFE_WRITE: расширяем список ──
    safe_write_block = textwrap.dedent("""\
    SAFE_WRITE = {
        # Holding registers (func 0x03 / 0x06 / 0x10) — безопасные для записи
        0x000C: 0x0002,  # TimeLedStatePulse
        0x000D: 0x000A,  # TimeLedStatePeriod
        0x000E: 0x1068,  # battery_charge_volt_max
        0x000F: 0x0CE4,  # battery_charge_volt_min
        0x0010: 0x0DAC,  # battery_charge_volt_lim_1
        0x0011: 0x0D48,  # battery_charge_volt_lim_2
        0x0014: 0x0000,  # PeriodTimeLora
        0x0015: 0x0000,  # Lora_Config_Flags
        0x0019: 0x0001,  # Immobility_Enable
        0x001A: 0x0078,  # Immobility_Window_s
        0x001B: 0x003C,  # Immobility_PreAlarm_s
        0x001C: 0x00FA,  # Immobility_Ths_mg
        0x001D: 0x0000,  # Devices_Expected
        0x001E: 0x0002,  # Alert_Flags
        0x0024: 0x0A00,  # Archiveinterval
        0x0025: 0x00,    # ArchiveRecording
        0x002B: 0x0000,  # temperature (float lo)
        0x002D: 0x0000,  # flag
    }""")

    # Заменяем существующий SAFE_WRITE или добавляем
    if 'SAFE_WRITE' in src:
        src = re.sub(
            r'SAFE_WRITE\s*=\s*\{[^}]*\}',
            safe_write_block,
            src,
            flags=re.DOTALL
        )
    else:
        # Вставляем после импортов
        src = re.sub(
            r'(import\s+serial\s*\n)',
            r'\1\n' + safe_write_block + '\n',
            src,
            count=1
        )

    if src != original:
        p.write_text(src, encoding="utf-8")
        print(f"  [OK] {p}")
    else:
        print(f"  [NO-CHANGE] {p}")

# ══════════════════════════════════════════════════════════════════════════════
# 4. tools/retry_frames.py
# ══════════════════════════════════════════════════════════════════════════════
def patch_retry_frames():
    p = ROOT / "tools" / "retry_frames.py"
    if not p.exists():
        print(f"  [SKIP] {p}"); return

    src = p.read_text(encoding="utf-8", errors="replace")
    backup(p)
    original = src

    # Кадр 0x0C → 10 байт
    src = re.sub(
        r'"64\s+0C\s+00\s+00\s+00\s+04"',
        '"64 0C 00 00 00 00 00 04"',
        src
    )
    src = re.sub(
        r'rd\(0x0C,\s*0x0000,\s*4,\s*slave\)',
        r'bytes([slave, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04]) + crc16_bytes(bytes([slave, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04]))',
        src
    )

    if src != original:
        p.write_text(src, encoding="utf-8")
        print(f"  [OK] {p}")
    else:
        print(f"  [NO-CHANGE] {p}")

# ══════════════════════════════════════════════════════════════════════════════
# 5. Документация
# ══════════════════════════════════════════════════════════════════════════════
DOC_ADDENDUM = textwrap.dedent("""\

---

## Форматы команд 0x0C / 0x0E / 0x0F (Patch 1)

### 0x0C — Чтение журнала (Read Log)

| Поле | Байт | Описание |
|------|------|----------|
| Slave | 0 | Адрес устройства (или 0 = broadcast) |
| Func | 1 | `0x0C` |
| Reserved | 2–5 | `0x00 0x00 0x00 0x00` (зарезервировано) |
| Start ptr | 6 | Начальный указатель (0 = с начала) |
| Count | 7 | Количество записей `cnt` |
| CRC | 8–9 | CRC-16 Modbus (Lo, Hi) |

**Итого запрос: 10 байт.**

**Ответ:** `[Slave, 0x0C, Byte_Count, Data(cnt×40), CRC_Lo, CRC_Hi]`  
Парсер ждёт `3 + cnt*40 + 2` байт.

### 0x0E — Запись во внутреннюю Flash (MCU)

Staging-режим: данные копируются в `cmd_state.write_buf`, выставляется
`int_write_pending = 1`. Фактическая запись — в Measure Task.

**Ответ: 6-байтное эхо** `[Slave, 0x0E, addr_hi, addr_lo, len_hi, len_lo]` —
мастер подтверждает приём пакета.

### 0x0F — Запись во внешнюю Flash (SPI NOR)

Аналогично 0x0E, но `ext_write_pending = 1`.

**Ответ: 6-байтное эхо.**

### Broadcast (адрес 0x00)

Контроллер **отвечает** на broadcast-запросы. Используется для опроса
прибора «1-на-1» без прописки адреса на линии.

### Указатель кольцевого буфера логов

* Указатель — монотонный `uint32_t`.
* Размер кольца задаётся `LOG_RING_SLOTS` (по умолчанию 32768 записей).
* Физический индекс = `monotonic_ptr & (LOG_RING_SLOTS - 1)`.

### Неизвестные команды

Неизвестный код функции **игнорируется** — ответ не формируется.
Это упрощает диагностику: мастер просто получает таймаут.
""")

def patch_docs():
    for name in ["docs/Command.md", "docs/ARCHITECTURE.md"]:
        p = ROOT / name
        if not p.exists():
            print(f"  [SKIP] {p}"); continue
        backup(p)
        src = p.read_text(encoding="utf-8", errors="replace")
        if "## Форматы команд 0x0C / 0x0E / 0x0F" not in src:
            src += "\n" + DOC_ADDENDUM
            p.write_text(src, encoding="utf-8")
            print(f"  [OK] {p}: добавлено описание форматов")
        else:
            print(f"  [ALREADY] {p}")

# ══════════════════════════════════════════════════════════════════════════════
# MAIN
# ══════════════════════════════════════════════════════════════════════════════
def main():
    print("=" * 70)
    print("  PATCH 1: Command_vector / Modbus fixes")
    print(f"  Backup → {BACKUP_DIR.resolve()}")
    print("=" * 70)

    patch_command_vector()
    patch_send_frames()
    patch_verify_registers()
    patch_retry_frames()
    patch_docs()

    print("\n" + "=" * 70)
    print("  ГОТОВО. Пересобери прошивку и запусти:")
    print("    py tools/send_frames.py COM4")
    print("    py tools/verify_registers.py COM4 --slave 100 --write")
    print("    py tools/retry_frames.py COM4")
    print("=" * 70)

if __name__ == "__main__":
    main()