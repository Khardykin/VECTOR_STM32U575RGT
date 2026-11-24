#!/usr/bin/env python3
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
