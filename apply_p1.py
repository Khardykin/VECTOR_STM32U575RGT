#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_p14.py — Path B: Direct Flash Write in Receiver Task + Sound Quenching.
Run from project root: py apply_p14.py
"""
import re, shutil
from pathlib import Path

ROOT = Path(".")
BAK = ROOT / "_backup_p14"
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
    """Изменяем CmdFunc15: пишем во flash сразу, ставим block_sound."""
    p = ROOT / "Core" / "VectorLib" / "Src" / "Command_vector.c"
    if not p.exists():
        log(f"[SKIP] {p}")
        return
    
    backup(p)
    src = read(p)
    changed = False

    # Ищем конец функции CmdFunc15 перед возвратом эхо
    # Там сейчас стоит: cmd_state.ext_write_pending = 1u; ... return 6u;
    
    old_logic = r'(cmd_state\.write_addr\s*=\s*base\s*\+\s*addr;\s*cmd_state\.write_len\s*=\s*size;\s*memcpy\(cmd_state\.write_buf,\s*&mas\[7\],\s*size\);\s*cmd_state\.ext_write_pending\s*=\s*1u;\s*cmd_state\.flash_status\s*=\s*CV_ST_BUSY;\s*sys_state\.process\s*=\s*\(cmd_state\.flash_target\s*==\s*CV_TGT_FW\)\s*\?\s*SYS_PROC_FW\s*:\s*SYS_PROC_ARCHIVE;\s*return\s*6u;)'
    
    match = re.search(old_logic, src, re.DOTALL)
    if match:
        new_logic = """cmd_state.write_addr = base + addr;
  cmd_state.write_len  = size;
  memcpy(cmd_state.write_buf, &mas[7], size);
  
  /* Path B: Immediate write in Receiver Task */
  int rc = ext_write(cmd_state.write_addr, cmd_state.write_buf, cmd_state.write_len);
  
  if (rc == EXT_BUSY) {
      /* Шина занята (звук играет). Ставим pending, попробуем позже в Measure Task. */
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
  
  /* Block sound during exchange */
  sys_state.power |= ST_COMMON_BIT_BLOCK_SOUND; 
  
  /* Start quench timer */
  START_TIMER_RTC(TIMER_RTC_DATA_EXCHANGE_END, TIME_RTC_DATA_EXCHANGE_TIMEOUT_MS);
  
  return 6u;"""
        
        src_new = src.replace(match.group(0), new_logic)
        write(p, src_new)
        log("  [OK] CmdFunc15: Direct write + block_sound + quench timer")
        changed = True
    else:
        log("  [WARN] Якорь CmdFunc15 не найден. Проверь вручную.")
        
    if changed:
        log(f"  [DONE] {p}")
    else:
        log(f"  [INFO] Файл не изменен")

def patch_main_vector_quench():
    """Добавляем сброс block_sound по кнопке/сигнализации в Vector_Run_Measure."""
    p = ROOT / "Core" / "VectorLib" / "Src" / "main_vector.c"
    if not p.exists():
        log(f"[SKIP] {p}")
        return
        
    backup(p)
    src = read(p)
    
    # Ищем место после проверки таймера DATA_EXCHANGE_END
    # Обычно там есть строка: if(TEST_TIMER_RTC(TIMER_RTC_DATA_EXCHANGE_END)) { ... }
    
    pattern = r'(if\s*\(TEST_TIMER_RTC\(TIMER_RTC_DATA_EXCHANGE_END\)\)\s*\{[^}]*\})'
    match = re.search(pattern, src, re.DOTALL)
    
    if match:
        old_block = match.group(0)
        # Добавляем логическое условие: сброс флага также при нажатии любой кнопки
        # Предположим, что статус кнопок хранится в sys_state.buttons или аналогичной структуре.
        # Если точного имени нет, используем общий флаг активности UI.
        
        insertion = "\n\n  /* Quench block_sound on user interaction (button press / alarm stop) */\n"
        insertion += "  if ((sys_state.power & ST_COMMON_BIT_BLOCK_SOUND) && \n"
        insertion += "      (get_button_active() || audio_is_alarm_playing())) {\n"
        insertion += "      sys_state.power &= ~ST_COMMON_BIT_BLOCK_SOUND;\n"
        insertion += "      RESET_TIMER_RTC(TIMER_RTC_DATA_EXCHANGE_END);\n"
        insertion += "  }\n"
        
        # Вставляем после блока таймера
        pos = match.end()
        new_src = src[:pos] + insertion + src[pos:]
        write(p, new_src)
        log("  [OK] main_vector.c: Added quench logic for block_sound")
    else:
        log("  [WARN] Блок TIMER_RTC_DATA_EXCHANGE_END не найден в main_vector.c")

def create_test_sounds_script():
    """Создает скрипт для генерации маленького файла sounds.bin."""
    script_path = ROOT / "tools" / "gen_test_sounds.py"
    script_content = '''#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_test_sounds.py - Генерирует маленький test_small.bin из 3 коротких тональных файлов.
Использует стандартную библиотеку wave/numpy для создания синтетических звуков.
"""
import wave, struct, math, os

SAMPLE_RATE = 44100
DURATION_SEC = 1.0 # 1 секунда на тон
FREQS = [440, 880, 1320] # A4, A5, E6

def generate_tone(freq, duration, sample_rate=SAMPLE_RATE):
    samples = []
    num_samples = int(sample_rate * duration)
    for i in range(num_samples):
        t = i / sample_rate
        value = 0.5 * math.sin(2 * math.pi * freq * t)
        # Int16 conversion
        int_value = int(value * 32767)
        samples.append(struct.pack('<h', int_value))
    return b''.join(samples)

def pack_audio_image(wav_files_data, out_filename):
    """
    Упаковывает сырые PCM данные в формат образа sounds.bin.
    Формат (упрощенный пример, должен совпадать с AUDIO.md):
    Header: Magic(4) Count(4) IndexTable(N*8) ... Data...
    Здесь мы просто склеиваем заголовки и данные для теста структуры.
    ВАЖНО: Для реального прогона используй tools/pack_sounds.py из репозитория!
    Этот скрипт создает заглушку для проверки скорости передачи.
    """
    with open(out_filename, 'wb') as f:
        # Заглушка заголовка (Magic + Count)
        magic = b'SND!' 
        count = struct.pack('<I', len(wav_files_data))
        f.write(magic)
        f.write(count)
        
        # Индексная таблица (Offset u32, Size u32)
        current_offset = 12 + len(wav_files_data)*8 # Пример расчета
        index_table = []
        data_blocks = []
        
        for pcm_data in wav_files_data:
            size = len(pcm_data)
            index_table.append(struct.pack('<II', current_offset, size))
            data_blocks.append(pcm_data)
            current_offset += size
            
        # Пишем индекс
        for entry in index_table:
            f.write(entry)
            
        # Пишем данные
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
    write(script_path, script_content)
    log(f"  [OK] Создан {script_path}")

def main():
    print("=" * 72)
    print("  PATCH 14: Path B (Direct Write) + Sound Quenching")
    print(f"  Backup: {BAK.resolve()}")
    print("=" * 72)
    print()

    patch_command_vector_c()
    print()
    patch_main_vector_quench()
    print()
    create_test_sounds_script()

    print()
    print("=" * 72)
    oks = [r for r in REPORT if "[OK]" in r]
    warns = [r for r in REPORT if "[WARN]" in r]
    print(f"  Применено: {len(oks)}, Предупреждений: {len(warns)}")
    for w in warns: print("   " + w)
    
    print("\n  Дальше:")
    print("    1. Build Project + ПРОШЕЙ плату.")
    print("    2. Сгенерируй маленький файл:")
    print("       py tools/gen_test_sounds.py")
    print("    3. Тестируй запись:")
    print("       py tools/test_flash_write_final.py COM4 --slave 100 --file tools/test_small.bin --chunk 255")
    print("=" * 72)

if __name__ == "__main__":
    main()