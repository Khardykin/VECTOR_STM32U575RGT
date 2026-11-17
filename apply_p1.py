#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_p15_v2.py - финальный звук как AlertSeq_PowerOn (отдельный сценарий,
# запускается при истечении таймера, ждём его завершения).
# Заменяет логику p15 (переключение статусов). Запуск ИЗ КОРНЯ: py apply_p15_v2.py
import re, os, sys

AH = "Core/VectorLib/Inc/Alert_vector.h"
AV = "Core/VectorLib/Src/Alert_vector.c"
MV = "Core/VectorLib/Src/Menu_vector.c"

def rd(p):
    with open(p, encoding="utf-8") as f: return f.read()

def wr(p, s):
    with open(p, "w", encoding="utf-8", newline="") as f: f.write(s)

for p in (AH, AV, MV):
    if not os.path.exists(p):
        print("[!] нет %s (запускай из корня)" % p); sys.exit(1)

def rep(p, old, new, cnt=1):
    s = rd(p)
    c = s.count(old)
    if c != cnt:
        print("[!] %s: якорь %d раз (ожидался %d):\n%s" % (p, c, cnt, old[:80]))
        sys.exit(1)
    wr(p, s.replace(old, new, cnt))

# ============================================================
# 1. Alert_vector.h: +extern AlertSeq_CountdownFinal
# ============================================================
h = rd(AH)
if "AlertSeq_CountdownFinal" in h:
    print("[=] Alert_vector.h: AlertSeq_CountdownFinal уже объявлен")
else:
    # Ищем объявление AlertSeq_PowerOn и добавляем после него
    old_h = "extern const alert_seq_t AlertSeq_PowerOn;"
    new_h = ("extern const alert_seq_t AlertSeq_PowerOn;\n"
             "extern const alert_seq_t AlertSeq_CountdownFinal;  /* p15 v2: финальный звук отсчёта */")
    if old_h not in h:
        print("[!] Alert_vector.h: не найден extern AlertSeq_PowerOn"); sys.exit(1)
    wr(AH, h.replace(old_h, new_h, 1))
    print("[+] Alert_vector.h: +extern AlertSeq_CountdownFinal")

# ============================================================
# 2. Alert_vector.c: +AlertSeq_CountdownFinal (одна фаза, 600 мс)
# ============================================================
a = rd(AV)
if "AlertSeq_CountdownFinal" in a:
    print("[=] Alert_vector.c: AlertSeq_CountdownFinal уже есть")
else:
    # Ищем определение AlertSeq_PowerOn и добавляем после него
    old_a = """const alert_seq_t AlertSeq_PowerOn =
{
  .ph    = s_poweron_phases,
  .n     = (uint8_t)(sizeof(s_poweron_phases) / sizeof(s_poweron_phases[0])),
  .prio  = ALERT_PRIO_UI,
  .count = 1u                       /* один прогон, как в Avis               */
};"""
    new_a = """const alert_seq_t AlertSeq_PowerOn =
{
  .ph    = s_poweron_phases,
  .n     = (uint8_t)(sizeof(s_poweron_phases) / sizeof(s_poweron_phases[0])),
  .prio  = ALERT_PRIO_UI,
  .count = 1u                       /* один прогон, как в Avis               */
};

/* p15 v2: финальный звук отсчёта (выключение / SOS). Одна фаза: длинный
   писк 600 мс со всеми каналами (звук + вибро + оба LED). Запускается один
   раз при истечении таймера, после чего движок сам останавливается
   (seq_finish), и Menu_CountdownTick выполняет действие.                    */
static const alert_phase_t s_countdown_final_phases[] =
{
  /*  маска каналов (alert_ch_t)                                          период пауза повт миг sound_idx */
  { (uint8_t)(ALERT_CH_BEEP | ALERT_CH_LED1 | ALERT_CH_LED2 | ALERT_CH_VIBRO),
                                                   600u,   0u, 1u, 0u, 0u },
};
const alert_seq_t AlertSeq_CountdownFinal =
{
  .ph    = s_countdown_final_phases,
  .n     = (uint8_t)(sizeof(s_countdown_final_phases) / sizeof(s_countdown_final_phases[0])),
  .prio  = ALERT_PRIO_ALARM,
  .count = 1u                       /* один прогон, как у PowerOn            */
};"""
    if old_a not in a:
        print("[!] Alert_vector.c: не найден AlertSeq_PowerOn"); sys.exit(1)
    wr(AV, a.replace(old_a, new_a, 1))
    print("[+] Alert_vector.c: +AlertSeq_CountdownFinal")

# ============================================================
# 3. Menu_vector.c: переписываем Menu_CountdownTick на новую логику
# ============================================================
m = rd(MV)
if "p15 v2: финальный сценарий" in m:
    print("[=] Menu_CountdownTick: уже в логике p15 v2"); sys.exit(0)

# Ищем текущий Menu_CountdownTick целиком
pat = re.compile(r"static void Menu_CountdownTick\(void\)\s*\{.*?\n\}", re.DOTALL)
cur = pat.search(m)
if not cur:
    print("[!] не найдена Menu_CountdownTick"); sys.exit(1)

new_fn = """static void Menu_CountdownTick(void)
{
  if (s_cnt_kind == 0u) { return; }

  uint8_t held = (s_cnt_kind == 1u)
      ? (uint8_t)((READ_PIN_IN(BUTTON_3) == VECTOR_KEY_PRESSED_LEVEL) ? 1u : 0u)
      : (uint8_t)((READ_PIN_IN(BUTTON_1) == VECTOR_KEY_PRESSED_LEVEL) ? 1u : 0u);
  if (held == 0u)
  {
    LOG_I(VLOG_M_SYS, "menu: countdown cancelled (button released)");
    countdown_cancel();
    Menu_GoTo(s_cnt_return);
    return;
  }

  uint8_t expired = (s_cnt_kind == 1u) ? (uint8_t)TEST_TIMER(TIMER_SHUTDOWN)
                                       : (uint8_t)TEST_TIMER(TIMER_PRESS_SOS);

  /* p15 v2: финальный сценарий запущен - ждём, пока он отработает
     (Alert_Busy() == 0), и только потом выполняем действие.                 */
  if (s_cnt_final != 0u)
  {
    if (Alert_Busy() != 0u) { return; }    /* сценарий ещё играет - ждём */
    if (s_cnt_kind == 1u)
    {
      RESET_TIMER(TIMER_SHUTDOWN);
      s_cnt_kind = 0u;
      s_cnt_final = 0u;
      Menu_TurnOff();
    }
    else
    {
      RESET_TIMER(TIMER_PRESS_SOS);
      s_cnt_kind = 0u;
      s_cnt_final = 0u;
      Sys_Cfg_Lock();
      SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_SOS);
      Sys_Cfg_Unlock();
#if CONFIG_LORA
      Lora_SetControlLoraBit(ST_LORA_BIT_SOS, 1u);
#endif
      Alert_Set(ALERT_SOS);
      Menu_ResetButtons();
      Menu_GoTo(s_cnt_return);
      LOG_W(VLOG_M_SYS, "menu: SOS set by hold countdown");
    }
    return;
  }

  if (expired == 0u) { return; }

  /* p15 v2: таймер истёк - гасим обычный статус и запускаем финальный
     сценарий (аналогично AlertSeq_PowerOn). Он отработает 600 мс и сам
     остановится; на следующем тике выполним действие.                        */
  if (s_cnt_kind == 1u)
  {
    Alert_Clear(ALERT_SHUTDOWN);
  }
  else
  {
    Alert_Clear(ALERT_SOS_COUNTDOWN);
  }
  Alert_Start(&AlertSeq_CountdownFinal);
  s_cnt_final = 1u;
  LOG_I(VLOG_M_SYS, "menu: countdown expired, final sound started");
}"""

m = m[:cur.start()] + new_fn + m[cur.end():]
wr(MV, m)
print("[+] Menu_CountdownTick: логика p15 v2 (AlertSeq_CountdownFinal)")
print("ГОТОВО. Собери проект (Debug).")