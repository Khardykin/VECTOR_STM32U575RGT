#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""gen_test_sounds.py - Creates test_small.bin with 3 short tones."""
import struct, math, os

SAMPLE_RATE = 44100
DURATION_SEC = 1.0
FREQS = [440, 880, 1320]

def generate_tone(freq, duration):
    samples = []
    for i in range(int(SAMPLE_RATE * duration)):
        t = i / SAMPLE_RATE
        value = 0.5 * math.sin(2 * math.pi * freq * t)
        samples.append(struct.pack('<h', int(value * 32767)))
    return b''.join(samples)

def pack_audio_image(wav_data_list, out):
    with open(out, 'wb') as f:
        f.write(b'SND!')
        f.write(struct.pack('<I', len(wav_data_list)))
        off = 12 + len(wav_data_list) * 8
        idx = []
        for d in wav_data_list:
            idx.append(struct.pack('<II', off, len(d)))
            off += len(d)
        for e in idx: f.write(e)
        for d in wav_data_list: f.write(d)
    print(f"Generated {out}: {os.path.getsize(out)} bytes")

if __name__ == "__main__":
    tones = [generate_tone(f, DURATION_SEC) for f in FREQS]
    pack_audio_image(tones, "tools/test_small.bin")
