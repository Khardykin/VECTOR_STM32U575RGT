#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_p13.py — Fix Erase Status Race Condition.
Ensure flash_status is BUSY before starting erase loop.
Run from project root: py apply_p13.py
"""
import re, shutil
from pathlib import Path

ROOT = Path(".")
BAK = ROOT / "_backup_p13"
REPORT = []

def log(msg):
    REPORT.append(msg)
    print(msg)

def backup(p):
    rel = p.relative_to(ROOT)
    dst = BAK / rel
    dst.parent.mkdir(parents=True, exist_ok=True)
    if not dst.exists():
        shutil.copy2(p, dst)

def read(p): return p.read_text(encoding="utf-8", errors="replace")
def write(p, s): p.write_text(s, encoding="utf-8")

def patch_command_vector_c():
    p = ROOT / "Core" / "VectorLib" / "Src" / "Command_vector.c"
    if not p.exists():
        log(f"[SKIP] {p}")
        return
    
    backup(p)
    src = read(p)
    changed = False

    # Ищем блок обработки erase для SOUND/LOG/FW
    # Нам нужно заменить весь блок if ((target == CV_TGT_SOUND) ...) { ... }
    
    pattern = r'(if \(\(target == CV_TGT_SOUND\) \|\| \(target == CV_TGT_FW\) \|\| \(target == CV_TGT_LOG\)\)\s*\{.*?\n\s*return;\s*\})'
    
    match = re.search(pattern, src, re.DOTALL)
    if match:
        old_block = match.group(0)
        
        replacement_code = """if ((target == CV_TGT_SOUND) || (target == CV_TGT_FW) || (target == CV_TGT_LOG))
    {
      uint32_t base;
      uint32_t limit;
      uint32_t a;

      if (target == CV_TGT_SOUND)   { base = SF_SOUNDS_BASE; limit = SF_SOUNDS_SIZE; }
      else if (target == CV_TGT_FW) { base = SF_BOOT_BASE;   limit = SF_BOOT_SIZE;   }
      else                          { base = SF_LOG_BASE;    limit = SF_LOG_SIZE;    }

      if ((cmd_state.flash_len == 0u) || (cmd_state.flash_len > limit))
      {
        cmd_state.flash_len = limit;
      }

      // КРИТИЧНОЕ ИСПРАВЛЕНИЕ:
      // Статус BUSY ДО начала цикла. 
      // Дефолт 0 (OK) опасен, так как CV_ST_OK == 0.
      // Скрипт должен видеть BUSY и ждать смены на OK.
      cmd_state.flash_status = CV_ST_BUSY; 

      for (a = 0u; a < cmd_state.flash_len; a += 4096u)
      {
        int rc = ext_erase_sector(base + cmd_state.flash_addr + a);

        if (rc == EXT_BUSY)
        {
          /* Шина занята. Сохраняем прогресс и выходим. */
          cmd_state.flash_addr  += a;
          cmd_state.flash_len   -= a;
          cmd_state.erase_req    = 1u;
          cmd_state.erase_mode   = mode;
          cmd_state.flash_status = CV_ST_BUSY; // Явно подтверждаем BUSY
          return;
        }

        if (rc != 0)
        {
          cmd_state.flash_status = CV_ST_IO;
          return;
        }
      }
      
      // Все сектора успешно стерты за этот проход
      cmd_state.flash_status = CV_ST_OK;
      cmd_state.erase_req    = 0u; // Сбрасываем флаг запроса
      return;
    }"""
    
    src_new = re.sub(pattern, replacement_code, src, count=1, flags=re.DOTALL)
    
    if src_new != src:
        write(p, src_new)
        log("  [OK] Исправлена логика Erase: Статус BUSY ставится ПЕРЕД циклом")
        changed = True
    else:
        log("  [WARN] Не удалось автоматически заменить блок. Проверь вручную.")
        
    if changed:
        log(f"  [DONE] {p}")
    else:
        log(f"  [INFO] Файл не изменен (или якорь не найден)")

def main():
    print("=" * 72)
    print("  PATCH 13: Fix Erase Status Race (Default 0 == OK bug)")
    print(f"  Backup: {BAK.resolve()}")
    print("=" * 72)
    print()

    patch_command_vector_c()

    print()
    print("=" * 72)
    oks = [r for r in REPORT if "[OK]" in r]
    warns = [r for r in REPORT if "[WARN]" in r]
    print(f"  Применено: {len(oks)}, Предупреждений: {len(warns)}")
    for w in warns: print("   " + w)
    print("\n  Дальше:")
    print("    1. Build Project + ПРОШЕЙ плату.")
    print("    2. Запусти запись снова:")
    print("       py tools/test_flash_write_final.py COM4 --slave 100 --file tools/sounds.bin --chunk 255")
    print("=" * 72)

if __name__ == "__main__":
    main()