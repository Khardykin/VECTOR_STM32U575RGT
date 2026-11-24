#!/usr/bin/env python3
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
