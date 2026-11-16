#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_p6.py - (1) убрать alert_hold_tick; (2) биты=источник, status_bits_tick читает биты->реестр;
# (3) +бит IMMOBILITY (+GAS_ALARM для экрана). Запуск ИЗ КОРНЯ: py apply_p6.py (поверх p5)
import re, os, sys
ST="Core/VectorLib/Inc/shared_types.h"; AH="Core/VectorLib/Inc/Alert_vector.h"; AV="Core/VectorLib/Src/Alert_vector.c"
def rd(p):
    with open(p,encoding="utf-8") as f: return f.read()
def wr(p,s):
    with open(p,"w",encoding="utf-8",newline="") as f: f.write(s)
for p in (ST,AH,AV):
    if not os.path.exists(p): print("[!] нет %s"%p); sys.exit(1)
def rep(p,old,new,cnt=1):
    s=rd(p); c=s.count(old)
    if c!=cnt: print("[!] %s: якорь %d раз (ожидался %d):\n%s"%(p,c,cnt,old[:80])); sys.exit(1)
    wr(p,s.replace(old,new,cnt))
s=rd(ST)
if "ST_COMMON_BIT_IMMOBILITY" not in s:
    rep(ST,"  ST_COMMON_BIT_SOS_GROUP,                       // SOS группы (от сервера, бит SOS_GROUP ControlLora)\n};",
           "  ST_COMMON_BIT_SOS_GROUP,                       // SOS группы (от сервера, бит SOS_GROUP ControlLora)\n"
           "  ST_COMMON_BIT_IMMOBILITY,                      // Неподвижность/отсутствие активности (LIS3DH)\n"
           "  ST_COMMON_BIT_GAS_ALARM,                       // Тревога по газу (пороги Avis / сервер)\n};")
    print("[+] shared_types: +IMMOBILITY, +GAS_ALARM")
else: print("[=] shared_types: биты уже есть")
a=rd(AV)
if "/* p6: биты -> реестр */" not in a:
    CONV = {
      "ALERT_EVACUATION":"ST_COMMON_BIT_EVACUATION",
      "ALERT_NO_GAS":"ST_COMMON_BIT_NO_GAS",
      "ALERT_NO_PPE":"ST_COMMON_BIT_NO_PPE",
      "ALERT_NO_BRACELET":"ST_COMMON_BIT_NO_BRACELET",
      "ALERT_GA_SENSOR_FAIL":"ST_COMMON_BIT_GA_SENSOR_FAIL",
      "ALERT_IMMOBILITY":"ST_COMMON_BIT_IMMOBILITY",
      "ALERT_BATTERY_CRIT":"ST_COMMON_BIT_CRITICAL_BATTERY",
      "ALERT_BATTERY_LOW":"ST_COMMON_BIT_CRITICAL_LOW_BATTERY",
    }
    for al,bit in CONV.items():
        a=re.sub(r"Alert_Set\(%s\)"%al,   "do{Sys_Cfg_Lock();SET_STATUS_COMMON_ERR_BIT(%s);Sys_Cfg_Unlock();}while(0)"%bit, a)
        a=re.sub(r"Alert_Clear\(%s\)"%al, "do{Sys_Cfg_Lock();CLEAR_STATUS_COMMON_ERR_BIT(%s);Sys_Cfg_Unlock();}while(0)"%bit, a)
    wr(AV,a); print("[+] источники переведены на биты")
    a=rd(AV)
    a=re.sub(r"/\* Автоочистка статусов с ОДНОРАЗОВЫМ источником.*?static void alert_hold_tick\(void\).*?\n\}\n", "", a, flags=re.DOTALL)
    a=a.replace("    alert_hold_tick();         /* автоочистка статусов с одноразовым источником */\n","")
    a=a.replace(".voice = ALERT_SND_EVACUATION, .hold_s = ALERT_EVAC_HOLD_S },",".voice = ALERT_SND_EVACUATION, .hold_s = 0u },")
    a=a.replace(".voice = ALERT_SND_MESSAGE, .hold_s = ALERT_MSG_HOLD_S },",".voice = ALERT_SND_MESSAGE, .hold_s = 0u },")
    wr(AV,a); print("[+] alert_hold_tick удалён; evac/message hold_s=0")
    a=rd(AV)
    new_sbt = """static void status_bits_tick(void)
{
  /* p6: биты -> реестр. Биты ST_COMMON_ERR - ИСТОЧНИК (их ставят battery_tick,
     device_presence_tick, gas_local_tick, immob, Lora-мост, меню); здесь ОДНО
     место, где по битам ведётся реестр индикации. Реестр сам биты НЕ пишет.   */
  if (TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_SOS)) { Alert_Set(ALERT_SOS); } else { Alert_Clear(ALERT_SOS); }
  if (TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_SOS_GROUP)) { Alert_Set(ALERT_SOS_GROUP); } else { Alert_Clear(ALERT_SOS_GROUP); }
  if (TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_EVACUATION)) { Alert_Set(ALERT_EVACUATION); } else { Alert_Clear(ALERT_EVACUATION); }
  if (TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_NO_GAS)) { Alert_Set(ALERT_NO_GAS); } else { Alert_Clear(ALERT_NO_GAS); }
  if (TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_NO_PPE)) { Alert_Set(ALERT_NO_PPE); } else { Alert_Clear(ALERT_NO_PPE); }
  if (TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_NO_BRACELET)) { Alert_Set(ALERT_NO_BRACELET); } else { Alert_Clear(ALERT_NO_BRACELET); }
  if (TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_GA_SENSOR_FAIL)) { Alert_Set(ALERT_GA_SENSOR_FAIL); } else { Alert_Clear(ALERT_GA_SENSOR_FAIL); }
  if (TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_IMMOBILITY)) { Alert_Set(ALERT_IMMOBILITY); } else { Alert_Clear(ALERT_IMMOBILITY); }
  if (TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_CRITICAL_BATTERY)) { Alert_Set(ALERT_BATTERY_CRIT); } else { Alert_Clear(ALERT_BATTERY_CRIT); }
  if (TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_CRITICAL_LOW_BATTERY)) { Alert_Set(ALERT_BATTERY_LOW); } else { Alert_Clear(ALERT_BATTERY_LOW); }
  /* Газ: реестр ведут gas_local_tick/Alert_LoraMessage; бит - зеркало для экрана. */
  Sys_Cfg_Lock();
  if (Alert_IsActive(ALERT_GAS_ALARM)) { SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_GAS_ALARM); }
  else                                 { CLEAR_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_GAS_ALARM); }
  Sys_Cfg_Unlock();
}
"""
    a=re.sub(r"static void status_bits_tick\(void\)\s*\{.*?\n\}\n", lambda m: new_sbt, a, count=1, flags=re.DOTALL)
    wr(AV,a); print("[+] status_bits_tick: биты -> реестр")
    a=rd(AV)
    if "if (top == ALERT_MESSAGE) { Alert_Clear(ALERT_MESSAGE); }" not in a:
        m=re.search(r"(uint8_t Alert_AckButton\(void\).*?)(return \(uint8_t\)top;)", a, re.DOTALL)
        if m:
            ins="  /* Сообщение - одноразовый статус без hold: квитирование снимает и статус. */\n  if (top == ALERT_MESSAGE) { Alert_Clear(ALERT_MESSAGE); }\n"
            a=a[:m.end(1)]+ins+a[m.end(1):]; wr(AV,a); print("[+] message: сброс по квитированию")
else:
    print("[=] Alert_vector.c: p6 уже применён")
print("ГОТОВО. Собери проект (Debug).")