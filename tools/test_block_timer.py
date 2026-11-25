#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_block_timer.py — снятие block_sound по таймеру TIMER_RTC_DATA_EXCHANGE_END.
Поднимает 0x0058=1, ждёт 35 с БЕЗ обмена по COM, затем читает зеркало и бит State.
Если audio_sound_block_reset() не обнуляет sys_state.block_sound — блокировка
воскреснет за 50 мс, и мы увидим block_sound=1 / State.bit0=1.
Запуск: py tools/test_block_timer.py COM4 --slave 100
"""
import sys, time
from test_sound_test import MB, R_BLOCK

port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
slave = 100
if "--slave" in sys.argv:
    slave = int(sys.argv[sys.argv.index("--slave") + 1])

mb = MB(port, slave)

def state_bit0():
    w = mb.rd(0x0003, 2)          # Config_common.State, u32
    return (w[0] | (w[1] << 16)) & 1

try:
    mb.wr(R_BLOCK, 1)
    time.sleep(0.3)
    print("установлено:   block_sound=%d  State.bit0=%d"
          % (mb.rd(R_BLOCK)[0], state_bit0()))
    print("жду 35 с без обмена по COM (таймер 30 с)...")
    time.sleep(35)
    bs, sb = mb.rd(R_BLOCK)[0], state_bit0()
    print("через 35 с:    block_sound=%d  State.bit0=%d" % (bs, sb))
    if bs == 0 and sb == 0:
        print("PASS: таймер снял блокировку, зеркало не воскресило бит")
    else:
        print("FAIL: блокировка не снялась либо воскресла ->"
              " audio_sound_block_reset() не обнуляет sys_state.block_sound")
finally:
    mb.wr(R_BLOCK, 0)
    mb.close()