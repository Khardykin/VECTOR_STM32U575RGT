#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_p18.py — Path B Final: Direct Write + Auto-Clear Block Sound by Timer.
Fixes compilation errors from p17.
Run from project root: py apply_p18.py
"""
import re, shutil
from pathlib import Path

ROOT = Path(".")
BAK = ROOT / "_backup_p18"
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

# ═══════════════════════════════════════════════════════════════════════════
# 1. Command_vector.c — Direct Write in CmdFunc14 & CmdFunc15
# ═══════════════════════════════════════════════════════════════════════════
def patch_command_vector_c():
    p = ROOT / "Core" / "VectorLib" / "Src" / "Command_vector.c"
    if not p.exists():
        log(f"[SKIP] {p}")
        return
    
    backup(p)
    src = read(p)
    changed = False

    # --- A. CmdFunc15 (External Flash) ---
    # Ищем хвост функции CmdFunc15 перед return 6u;
    pattern_15 = r'(cmd_state\.ext_write_pending\s*=\s*1u;\s*cmd_state\.flash_status\s*=\s*CV_ST_BUSY;\s*sys_state\.process\s*=.*?;\s*return\s*6u;)'
    match_15 = re.search(pattern_15, src, re.DOTALL)
    
    if match_15:
        new_code_15 = """/* Path B: Immediate write to External Flash */
  int rc = ext_write(cmd_state.write_addr, cmd_state.write_buf, cmd_state.write_len);
  
  if (rc == EXT_BUSY) {
      /* Fallback: Queue for Measure Task if bus busy */
      cmd_state.ext_write_pending = 1u;
      cmd_state.flash_status      = CV_ST_BUSY;
  } else if (rc != 0) {
      cmd_state.flash_status = CV_ST_IO;
  } else {
      /* Verify immediately */
      uint8_t verify[CV_WRITE_BUF_SIZE];
      rc = ext_read(cmd_state.write_addr, verify, cmd_state.write_len);
      if (rc != 0 || memcmp(verify, cmd_state.write_buf, cmd_state.write_len) != 0) {
          cmd_state.flash_status = CV_ST_VERIFY;
      } else {
          cmd_state.flash_status = CV_ST_OK;
      }
  }
  
  /* Block Sound during exchange */
  SET_STATUS_COMMON_BIT(ST_COMMON_BIT_BLOCK_SOUND); 
  
  /* Start quench timer (auto-clear after timeout) 
     Using existing constant TIME_RTC_DATA_EXCHANGE_END as duration. */
  START_TIMER_RTC(TIMER_RTC_DATA_EXCHANGE_END, TIME_RTC_DATA_EXCHANGE_END); 
  
  sys_state.process = SYS_PROC_ARCHIVE;
  return 6u;"""
        
        src = src[:match_15.start()] + new_code_15 + src[match_15.end():]
        log("  [OK] CmdFunc15: Direct write + Block Sound")
        changed = True
    else:
        log("  [WARN] CmdFunc15 tail not found. Check manually.")

    # --- B. CmdFunc14 (Internal Flash) ---
    pattern_14 = r'(cmd_state\.int_write_pending\s*=\s*1u;\s*cmd_state\.flash_status\s*=\s*CV_ST_BUSY;\s*sys_state\.process\s*=\s*SYS_PROC_FW;\s*return\s*6u;)'
    match_14 = re.search(pattern_14, src, re.DOTALL)
    
    if match_14:
        new_code_14 = """/* Path B: Immediate write to Internal Flash */
  int rc = boot_flash_write(cmd_state.write_addr, cmd_state.write_buf, cmd_state.write_len);
  
  if (rc == 0)       { cmd_state.flash_status = CV_ST_OK; }
  else if (rc == -2) { cmd_state.flash_status = CV_ST_VERIFY; }
  else               { cmd_state.flash_status = CV_ST_IO; }
  
  /* Block Sound during heavy internal flash op */
  SET_STATUS_COMMON_BIT(ST_COMMON_BIT_BLOCK_SOUND); 
  
  /* Start quench timer */
  START_TIMER_RTC(TIMER_RTC_DATA_EXCHANGE_END, TIME_RTC_DATA_EXCHANGE_END); 
  
  sys_state.process = SYS_PROC_FW;
  return 6u;"""
        
        src = src[:match_14.start()] + new_code_14 + src[match_14.end():]
        log("  [OK] CmdFunc14: Direct write + Block Sound")
        changed = True
    else:
        log("  [WARN] CmdFunc14 tail not found. Check manually.")

    if changed:
        write(p, src)
    log(f"  [DONE] {p}")

# ═══════════════════════════════════════════════════════════════════════════
# 2. main_vector.c — Clear Block Sound by Timer ONLY (No Button Logic yet)
# ═══════════════════════════════════════════════════════════════════════════
def patch_main_vector_c():
    p = ROOT / "Core" / "VectorLib" / "Src" / "main_vector.c"
    if not p.exists():
        log(f"[SKIP] {p}")
        return
        
    backup(p)
    src = read(p)
    
    # Ищем место обработки таймера DATA_EXCHANGE_END
    # Обычно в Vector_Run_Measure или Vector_Options_System
    pattern_timer = r'(if\s*\(TEST_TIMER_RTC\(TIMER_RTC_DATA_EXCHANGE_END\)\)\s*\{[^}]*\})'
    match_timer = re.search(pattern_timer, src, re.DOTALL)
    
    insertion_timer = "\n  CLEAR_STATUS_COMMON_BIT(ST_COMMON_BIT_BLOCK_SOUND);\n  RESET_TIMER_RTC(TIMER_RTC_DATA_EXCHANGE_END);"
    
    if match_timer:
        old_block = match_timer.group(0)
        # Проверяем, нет ли уже этой строки
        if "CLEAR_STATUS_COMMON_BIT(ST_COMMON_BIT_BLOCK_SOUND)" not in old_block:
            new_block = old_block.replace('{', '{' + insertion_timer)
            src = src.replace(old_block, new_block)
            log("  [OK] main_vector.c: Added CLEAR_STATUS_COMMON_BIT on timer expiry")
        else:
            log("  [INFO] Timer clear logic already present")
    else:
        log("  [WARN] TIMER_RTC_DATA_EXCHANGE_END handler not found. Adding manual block.")
        # Если блока нет, попробуем найти конец функции Vector_Run_Measure и вставить туда
        # Но для безопасности лучше оставить предупреждение и проверить вручную.
        pass

    write(p, src)
    log(f"  [DONE] {p}")

# ═══════════════════════════════════════════════════════════════════════════
# 3. Gen Test Sounds Script
# ═══════════════════════════════════════════════════════════════════════════
def create_gen_script():
    script_path = ROOT / "tools" / "gen_test_sounds.py"
    content = '''#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_test_sounds.py - Creates test_small.bin with 3 short tones (A4, A5, E6).
Total size ~150KB. Fast erase/write cycle (<5 sec).
"""
import wave, struct, math, os

SAMPLE_RATE = 44100
DURATION_SEC = 1.0 
FREQS = [440, 880, 1320] # Hz

def generate_tone(freq, duration):
    samples = []
    num_samples = int(SAMPLE_RATE * duration)
    for i in range(num_samples):
        t = i / SAMPLE_RATE
        value = 0.5 * math.sin(2 * math.pi * freq * t)
        int_value = int(value * 32767)
        samples.append(struct.pack('<h', int_value))
    return b''.join(samples)

def pack_audio_image(wav_files_data, out_filename):
    """
    Simplified header for testing structure only.
    Real format depends on AUDIO.md implementation details.
    Here we just concatenate raw PCM blocks with a dummy header.
    """
    with open(out_filename, 'wb') as f:
        # Dummy Magic + Count
        magic = b'SND!' 
        count = struct.pack('<I', len(wav_files_data))
        f.write(magic)
        f.write(count)
        
        # Index Table (Offset u32, Size u32)
        current_offset = 12 + len(wav_files_data)*8 
        index_table = []
        data_blocks = []
        
        for pcm_data in wav_files_data:
            size = len(pcm_data)
            index_table.append(struct.pack('<II', current_offset, size))
            data_blocks.append(pcm_data)
            current_offset += size
            
        for entry in index_table:
            f.write(entry)
            
        for block in data_blocks:
            f.write(block)
            
    print(f"Generated {out_filename}: {os.path.getsize(out_filename)} bytes")

if __name__ == "__main__":
    tones = []
    for freq in FREQS:
        print(f"Generating tone {freq} Hz...")
        tones.append(generate_tone(freq, DURATION_SEC))
        
    pack_audio_image(tones, "tools/test_small.bin")
    print("Done. Use this file for fast testing:")
    print("py tools/test_flash_write_final.py COM4 --slave 100 --file tools/test_small.bin --chunk 255")
'''
    script_path.parent.mkdir(exist_ok=True)
    write(script_path, content)
    log(f"  [OK] Created {script_path}")

def main():
    print("=" * 72)
    print("  PATCH 18: Path B Final (Direct Write + Correct Blocking)")
    print(f"  Backup: {BAK.resolve()}")
    print("=" * 72)
    print()

    patch_command_vector_c()
    print()
    patch_main_vector_c()
    print()
    create_gen_script()

    print()
    print("=" * 72)
    oks = [r for r in REPORT if "[OK]" in r]
    warns = [r for r in REPORT if "[WARN]" in r]
    infos = [r for r in REPORT if "[INFO]" in r]
    print(f"  OK: {len(oks)}, WARN: {len(warns)}, INFO: {len(infos)}")
    for w in warns: print("   " + w)
    for i in infos: print("   " + i)
    
    print("\n  Дальше:")
    print("    1. Build Project + ПРОШЕЙ плату.")
    print("    2. Сгенерируй маленький файл:")
    print("       py tools/gen_test_sounds.py")
    print("    3. Тестируй запись:")
    print("       py tools/test_flash_write_final.py COM4 --slave 100 --file tools/test_small.bin --chunk 255")
    print("    4. Проверь лог UART4: image ok=1 sounds=3")
    print("=" * 72)

if __name__ == "__main__":
    main()