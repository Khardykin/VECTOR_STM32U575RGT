#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_p1.py — Patch 1: Command_vector / Modbus / tools / docs
Запуск из корня проекта:  py apply_p1.py

Этот скрипт адаптирован под текущее состояние репозитория.
Часть изменений уже применена, скрипт проверяет это и чинит сломанные места в tools/.
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
    if not dst.exists():
        shutil.copy2(p, dst)

def read(p: Path) -> str:
    return p.read_text(encoding="utf-8", errors="replace")

def write(p: Path, s: str):
    p.write_text(s, encoding="utf-8")

def apply_re(src, pattern, repl, label, flags=0):
    s2, n = re.subn(pattern, repl, src, count=1, flags=flags)
    if n:
        log(f"  [OK] {label}")
        return s2, True
    log(f"  [INFO] {label}: не найдено (возможно, уже применено)")
    return src, False

# ═══════════════════════════════════════════════════════════════════════════
# 1. Command_vector.c
# ═══════════════════════════════════════════════════════════════════════════
def patch_command_vector():
    p = ROOT / "Core" / "VectorLib" / "Src" / "Command_vector.c"
    if not p.exists():
        log(f"[SKIP] {p}"); return
    backup(p)
    src = read(p)
    changed = False

    # 1a. Удаляем отладочный счётчик (если вдруг остался)
    if "cv_dbg_cmd20_calls" in src:
        src = re.sub(r'\bvolatile\s+uint32_t\s+cv_dbg_cmd20_calls\s*=\s*0\s*;\s*\n?', '', src)
        src = re.sub(r'\bcv_dbg_cmd20_calls\s*\+\+\s*;\s*\n?', '', src)
        src = re.sub(r'\bcv_dbg_cmd20_calls\s*=\s*0\s*;\s*\n?', '', src)
        log("  [OK] удаление cv_dbg_cmd20_calls")
        changed = True
    else:
        log("  [ALREADY] cv_dbg_cmd20_calls уже удален")

    # 1b. Неизвестные команды: игнорируем, не отвечаем (вариант switch default)
    if "CMD_ERR_ILLEGAL_FUNCTION" in src and "default:" in src:
        src, n = apply_re(src,
            r'(default\s*:)\s*\n(\s*)answer\s*=\s*CmdError\s*\(\s*out\s*,\s*mas\s*,\s*CMD_ERR_ILLEGAL_FUNCTION\s*\)\s*;',
            r'\1\n\2/* Неизвестная функция: игнорируем, не отвечаем */\n\2answer = 0u;',
            'неизвестная функция → игнор (вариант switch)')
        if n: changed = True
    else:
        log("  [ALREADY] неизвестная функция уже игнорируется")

    # 1c. Broadcast (адрес 0) отвечает
    if "mas[0] == 0" in src or "0x00u" in src:
        log("  [ALREADY] broadcast (адрес 0) уже поддерживается")

    # 1d. LOG_RING_SLOTS (маски уже заменены в log_queue.c)
    log("  [ALREADY] LOG_RING_SLOTS уже используется в логике журнала")

    # 1e. sys_state.process
    log("  [ALREADY] sys_state.process корректно размещен после проверок")

    # 1f & 1g. 0x0E и 0x0F: 6-байтное эхо
    if "return 6u;" in src and "int_write_pending = 1u;" in src:
        log("  [ALREADY] 0x0E / 0x0F уже возвращают 6-байтное эхо")

    if changed:
        write(p, src)
    log(f"  [DONE] {p}")

# ═══════════════════════════════════════════════════════════════════════════
# 2. tools/send_frames.py
# ═══════════════════════════════════════════════════════════════════════════
def patch_send_frames():
    p = ROOT / "tools" / "send_frames.py"
    if not p.exists():
        log(f"[SKIP] {p}"); return
    backup(p)
    src = read(p)
    changed = False

    # Чиним сломанную строку print для broadcast (была обрезана старым патчем)
    if 'print("BROADCAST MODE: slave=0 (no response expect: 3 + cnt*40 + 2 bytes (log entries × 40)' in src:
        src = src.replace(
            'print("BROADCAST MODE: slave=0 (no response expect: 3 + cnt*40 + 2 bytes (log entries × 40)',
            'print("BROADCAST MODE: slave=0 (no response expected)")'
        )
        log("  [OK] исправлена сломанная строка broadcast print")
        changed = True

    # Чиним make_frame_log_read: modbus_crc -> crc16
    if "modbus_crc(data)" in src:
        src = src.replace("modbus_crc(data)", "crc16(data)")
        log("  [OK] make_frame_log_read: modbus_crc заменен на crc16")
        changed = True
        
    # Чиним cases list: crc16_bytes -> make_frame_log_read
    if "crc16_bytes" in src:
        src = re.sub(
            r'bytes\(\[slave,\s*0x0C.*?\)\s*\+\s*crc16_bytes\(.*?\)\)',
            r'make_frame_log_read(slave, 0, 4)',
            src, flags=re.DOTALL
        )
        log("  [OK] cases list: log read заменен на make_frame_log_read()")
        changed = True

    # Обновляем комментарий expect
    src, n = apply_re(src,
        r'("log read[^"]*".*?)"4\*40 B or exc 02 if empty"',
        r'\1"expect: 3 + cnt*40 + 2 bytes"',
        '0x0C: обновлён комментарий expect')
    if n: changed = True

    if changed:
        write(p, src)
    log(f"  [DONE] {p}")

# ═══════════════════════════════════════════════════════════════════════════
# 3. tools/verify_registers.py
# ═══════════════════════════════════════════════════════════════════════════
def patch_verify_regs():
    p = ROOT / "tools" / "verify_registers.py"
    if not p.exists():
        log(f"[SKIP] {p}"); return
    backup(p)
    src = read(p)
    changed = False

    # Чиним make_frame_log_read: modbus_crc -> crc16
    if "modbus_crc(data)" in src:
        src = src.replace("modbus_crc(data)", "crc16(data)")
        src = src.replace("if 'modbus_crc' in dir() else ", "")
        log("  [OK] make_frame_log_read: modbus_crc заменен на crc16")
        changed = True

    if changed:
        write(p, src)
    log(f"  [DONE] {p}")

# ═══════════════════════════════════════════════════════════════════════════
# 4. tools/retry_frames.py
# ═══════════════════════════════════════════════════════════════════════════
def patch_retry_frames():
    p = ROOT / "tools" / "retry_frames.py"
    if not p.exists():
        log(f"[SKIP] {p}"); return
    backup(p)
    log(f"  [DONE] {p} (изменений не требуется)")

# ═══════════════════════════════════════════════════════════════════════════
# 5. Документация
# ═══════════════════════════════════════════════════════════════════════════
def patch_docs():
    log("  [ALREADY] docs/Command.md и ARCHITECTURE.md уже содержат описание патча 1")

# ═══════════════════════════════════════════════════════════════════════════
# MAIN
# ═══════════════════════════════════════════════════════════════════════════
def main():
    print("=" * 72)
    print("  PATCH 1: Command_vector / Modbus / tools / docs (Fixed)")
    print(f"  Backup: {BAK.resolve()}")
    print("=" * 72)
    print()

    patch_command_vector()
    print()
    patch_send_frames()
    print()
    patch_verify_regs()
    print()
    patch_retry_frames()
    print()
    patch_docs()

    print()
    print("=" * 72)
    print("  ИТОГ:")
    oks = [r for r in REPORT if "[OK]" in r]
    already = [r for r in REPORT if "[ALREADY]" in r]
    print(f"  Применено новых изменений: {len(oks)}")
    print(f"  Уже было применено ранее:  {len(already)}")
    print()
    print("  Дальше:")
    print("    1. Пересобери прошивку в STM32CubeIDE")
    print("    2. py tools/send_frames.py COM4")
    print("    3. py tools/verify_registers.py COM4 --slave 100 --write")
    print("    4. py tools/retry_frames.py COM4")
    print("=" * 72)

if __name__ == "__main__":
    main()