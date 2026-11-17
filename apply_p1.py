#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_p20.py - (1) SHUTDOWN/SOS_COUNTDOWN выше SOS/SOS_GROUP;
# (2) убрать ALERT_SHUTDOWN_FINAL и ALERT_SOS_COUNTDOWN_FINAL.
# Запуск ИЗ КОРНЯ: py apply_p20.py (идемпотентен)
import re, os, sys

AH = "Core/VectorLib/Inc/Alert_vector.h"
AV = "Core/VectorLib/Src/Alert_vector.c"
MV = "Core/VectorLib/Src/Menu_vector.c"

def rd(p):
    with open(p, encoding="utf-8") as f: return f.read()
def wr(p, s):
    with open(p, "w", encoding="utf-8", newline="") as f: f.write(s)

for p in (AH, AV, MV):
    if not os.path.exists(p): print("[!] нет %s" % p); sys.exit(1)

# ============================================================
# 1. Alert_vector.h: переставить приоритет + убрать FINAL
# ============================================================
h = rd(AH)
if "ALERT_SHUTDOWN_FINAL" not in h and re.search(r'ALERT_NONE\s*=\s*0,\s*\n[^A]*ALERT_SHUTDOWN,', h):
    print("[=] Alert_vector.h: приоритет уже правильный, FINAL убраны")
else:
    old_enum = """  ALERT_NONE = 0,
  /* ПРИОРИТЕТ = порядок в enum (alert_top_indication идёт от младшего id):
     SOS -> отсчёт выключения -> отсчёт SOS -> эвакуация -> газ -> неподвижность
     -> комплект (нет ГА/СИЗ/браслета, один ритм) -> отказ сенсора ГА ->
     прибор (крит. разряд, ошибки, низкий заряд) -> сообщение.               */
  ALERT_SOS,              /* SOS свой (кнопка, бит SOS в StateErr)        */
  ALERT_SOS_GROUP,        /* SOS группы (от сервера, бит SOS_GROUP)       */
  ALERT_SHUTDOWN,         /* обратный отсчёт выключения (удержание кнопки)  */
  ALERT_SOS_COUNTDOWN,    /* обратный отсчёт SOS (удержание кнопки)         */
  ALERT_SHUTDOWN_FINAL,   /* p15: последняя секунда выключения (600 мс)     */
  ALERT_SOS_COUNTDOWN_FINAL, /* p15: последняя секунда SOS (600 мс)         */
  ALERT_EVACUATION,       /* «ВСЕМ НА ВЫХОД» (бит EVACUATION от сервера)    */"""

    new_enum = """  ALERT_NONE = 0,
  /* ПРИОРИТЕТ = порядок в enum (alert_top_indication идёт от младшего id):
     p20: отсчёт выключения/включения — высший (таймер действия пользователя,
     не должен прерываться серверными тревогами) -> SOS -> SOS группы ->
     эвакуация -> газ -> неподвижность -> комплект (нет ГА/СИЗ/браслета)
     -> отказ сенсора ГА -> прибор (крит. разряд, ошибки, низкий заряд)
     -> сообщение.                                                          */
  ALERT_SHUTDOWN,         /* p20: обратный отсчёт выключения (удержание кнопки) */
  ALERT_SOS_COUNTDOWN,    /* p20: обратный отсчёт SOS (удержание кнопки)        */
  ALERT_SOS,              /* SOS свой (кнопка, бит SOS в StateErr)              */
  ALERT_SOS_GROUP,        /* SOS группы (от сервера, бит SOS_GROUP)             */
  ALERT_EVACUATION,       /* «ВСЕМ НА ВЫХОД» (бит EVACUATION от сервера)        */"""

    if old_enum not in h:
        print("[!] Alert_vector.h: якорь не совпал, проверь точный текст"); sys.exit(1)
    wr(AH, h.replace(old_enum, new_enum, 1))
    print("[+] Alert_vector.h: приоритет сдвинут + FINAL убраны")

# ============================================================
# 2. Alert_vector.c: убрать FINAL из s_desc
# ============================================================
a = rd(AV)
if "[ALERT_SHUTDOWN_FINAL]" not in a and "[ALERT_SOS_COUNTDOWN_FINAL]" not in a:
    print("[=] Alert_vector.c: FINAL в s_desc уже убраны")
else:
    # Удаляем блоки через regex (гибко, под любой формат)
    a2 = re.sub(
        r'\s*/\*[^*]*p15[^*]*финальн[^*]*\*/\s*'
        r'\[ALERT_SHUTDOWN_FINAL\]\s*=\s*\{[^}]+\},\s*'
        r'\[ALERT_SOS_COUNTDOWN_FINAL\]\s*=\s*\{[^}]+\},',
        '', a, count=1, flags=re.DOTALL)
    if a2 == a:
        # Пробуем без комментария
        a2 = re.sub(r'\s*\[ALERT_SHUTDOWN_FINAL\]\s*=\s*\{[^}]+\},', '', a, count=1, flags=re.DOTALL)
        a2 = re.sub(r'\s*\[ALERT_SOS_COUNTDOWN_FINAL\]\s*=\s*\{[^}]+\},', '', a2, count=1, flags=re.DOTALL)
    if a2 == a:
        print("[!] Alert_vector.c: не удалось удалить FINAL"); sys.exit(1)
    wr(AV, a2)
    print("[+] Alert_vector.c: FINAL удалены из s_desc")

# ============================================================
# 3. Menu_vector.c: убрать Alert_Clear(ALERT_*_FINAL)
# ============================================================
m = rd(MV)
if "ALERT_SHUTDOWN_FINAL" not in m and "ALERT_SOS_COUNTDOWN_FINAL" not in m:
    print("[=] Menu_vector.c: FINAL уже убраны")
else:
    m2 = re.sub(r'\s*Alert_Clear\(ALERT_SHUTDOWN_FINAL\);[^\n]*', '', m)
    m2 = re.sub(r'\s*Alert_Clear\(ALERT_SOS_COUNTDOWN_FINAL\);[^\n]*', '', m2)
    if m2 == m:
        print("[!] Menu_vector.c: не удалось удалить Clear FINAL"); sys.exit(1)
    wr(MV, m2)
    print("[+] Menu_vector.c: Alert_Clear(ALERT_*_FINAL) убраны")

print("ГОТОВО. Собери проект (Debug).")