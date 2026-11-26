#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
who_is_beeping.py — кто возьмёт индикацию, когда тест её освободит.
Читает Config_common.StateErr ОДИН раз и расшифровывает биты.
COM должен быть свободен (закрой терминал/второй скрипт).
Запуск: py tools/who_is_beeping.py COM4 --slave 100
"""
import sys
from test_sound_test import MB

NAMES = ["CRIT_LOW_BAT", "ERR_STS4", "ERR_NBIOT", "ERR_BLE", "ERR_GPS",
         "ERR_GSM", "ERR_LORA", "ERR_LCD", "CRIT_BAT", "SOS", "EVACUATION",
         "NO_GAS", "NO_PPE", "NO_BRACELET", "GA_SENSOR_FAIL", "SOS_GROUP",
         "IMMOBILITY", "GAS_ALARM"]
# у этих burst > 0 -> ОДНОКРАТНАЯ ПАЧКА писков (несколько раз подряд)
BURST = {"CRIT_LOW_BAT", "CRIT_BAT", "ERR_STS4", "ERR_NBIOT", "ERR_BLE",
         "ERR_GPS", "ERR_GSM", "ERR_LORA", "ERR_LCD", "GA_SENSOR_FAIL"}

port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
slave = 0
if "--slave" in sys.argv:
    slave = int(sys.argv[sys.argv.index("--slave") + 1])

mb = MB(port, slave)
try:
    w = mb.rd(0x0005, 2)                  # StateErr, u32
    err = w[0] | (w[1] << 16)
    s = mb.state()
    print("StateErr = 0x%08X   sound_test: active=%d source=%d last=%d"
          % (err, s["active"], s["source"], s["last"]))
    if err == 0:
        print("Фоновых статусов НЕТ -> писк идёт не от реестра."
              " Смотри лог UART4: строку 'alert: indication start [...]'."
              " Если её нет — источник вне Alert (проверь VECTOR_AUDIO_BOOT_PLAY).")
    else:
        print("Активные статусы (первый по порядку = лидер индикации):")
        for i, n in enumerate(NAMES):
            if err & (1 << i):
                print("   bit %-2d %-16s %s" % (i, n, "[ПАЧКА писков]" if n in BURST else "[непрерывно]"))
        lead = next(n for i, n in enumerate(NAMES) if err & (1 << i))
        print("\nКогда тест завершится, индикацию возьмёт: %s" % lead)
finally:
    mb.close()