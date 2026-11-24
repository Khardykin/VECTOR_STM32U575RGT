#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_p10.py — Fix Erase Logic: Don't set OK before loop completes.
Run from project root: py apply_p10.py
"""
import re, shutil
from pathlib import Path

ROOT = Path(".")
BAK = ROOT / "_backup_p10"
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
    # Паттерн: cmd_state.flash_status = CV_ST_OK; сразу перед for (...)
    pattern = r'(if \(\(target == CV_TGT_SOUND\) \|\| \(target == CV_TGT_FW\) \|\| \(target == CV_TGT_LOG\)\)\s*\{\s*uint32_t base;\s*uint32_t limit;\s*uint32_t a;\s*.*?cmd_state\.flash_status = CV_ST_OK;\s*for \(a = 0u; a < cmd_state\.flash_len; a \+= 4096u\))'
    
    match = re.search(pattern, src, re.DOTALL)
    if match:
        old_block = match.group(0)
        
        # Замена: убираем установку OK до цикла
        new_block = old_block.replace(
            "cmd_state.flash_status = CV_ST_OK;\n      for (a = 0u; a < cmd_state.flash_len; a += 4096u)",
            "// Status remains BUSY until all sectors erased\n      for (a = 0u; a < cmd_state.flash_len; a += 4096u)"
        )
        
        # Добавляем установку OK ПОСЛЕ цикла
        # Ищем конец блока else if ((addr + size) > limit) ... return; }
        # Нужно найти закрывающую скобку после цикла for
        
        # Проще: найдем весь блок if target==... и заменим его целиком на исправленную версию
        
        full_pattern = r'(if \(\(target == CV_TGT_SOUND\) \|\| \(target == CV_TGT_FW\) \|\| \(target == CV_TGT_LOG\)\)\s*\{.*?\n\s*return;\s*\})'
        
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

      // НЕ ставим OK здесь! Ждем завершения всех секторов
      
      for (a = 0u; a < cmd_state.flash_len; a += 4096u)
      {
        int rc = ext_erase_sector(base + cmd_state.flash_addr + a);

        if (rc == EXT_BUSY)
        {
          /* resume from this offset on the next pass */
          cmd_state.flash_addr  += a;
          cmd_state.flash_len   -= a;
          cmd_state.erase_req    = 1u;
          cmd_state.erase_mode   = mode;
          cmd_state.flash_status = CV_ST_BUSY;
          return;
        }

        if (rc != 0)
        {
          cmd_state.flash_status = CV_ST_IO;
          return;
        }
      }
      
      // Все сектора стерты успешно
      cmd_state.flash_status = CV_ST_OK;
      return;
    }"""
    
    src_new = re.sub(full_pattern, replacement_code, src, count=1, flags=re.DOTALL)
    
    if src_new != src:
        write(p, src_new)
        log("  [OK] Исправлена логика Erase: статус OK ставится только после полного цикла")
        changed = True
    else:
        log("  [WARN] Не удалось автоматически заменить блок. Проверь вручную.")
        
    if changed:
        log(f"  [DONE] {p}")
    else:
        log(f"  [INFO] Файл не изменен (или якорь не найден)")

def main():
    print("=" * 72)
    print("  PATCH 10: Fix Erase Logic (Status OK only after completion)")
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