#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_p5.py - (1) SOS два типа: SOS (свой, кнопка) + SOS_GROUP (от сервера);
# (2) газ-статус не гаснет по таймауту - снимается ДРУГИМ статусом от сервера;
# (3) обратные отсчёты - отдельные экраны SCREEN_COUNTDOWN_* (вызов LCD_* в Menu_Draw).
# Запуск ИЗ КОРНЯ:  py apply_p5.py   (идемпотентен; поверх p4)
import re, os, sys

ST="Core/VectorLib/Inc/shared_types.h"
AH="Core/VectorLib/Inc/Alert_vector.h"
AV="Core/VectorLib/Src/Alert_vector.c"
LO="Core/VectorLib/Src/Lora_S7678S.c"
MH="Core/VectorLib/Inc/Menu_vector.h"
MV="Core/VectorLib/Src/Menu_vector.c"
TI="Core/VectorLib/Src/TFT/TFT_indicator.c"

def rd(p):
    with open(p,encoding="utf-8") as f: return f.read()
def wr(p,s):
    with open(p,"w",encoding="utf-8",newline="") as f: f.write(s)
for p in (ST,AH,AV,LO,MH,MV,TI):
    if not os.path.exists(p): print("[!] нет %s"%p); sys.exit(1)
def rep(p,old,new,cnt=1):
    s=rd(p); c=s.count(old)
    if c!=cnt: print("[!] %s: якорь %d раз (ожидался %d):\n%s"%(p,c,cnt,old[:80])); sys.exit(1)
    wr(p,s.replace(old,new,cnt))

# ---- 1a. shared_types: +SOS_GROUP ----
if "ST_COMMON_BIT_SOS_GROUP" not in rd(ST):
    rep(ST,"  ST_COMMON_BIT_GA_SENSOR_FAIL,                  // Отказ сенсора ГА (бит 6 ST_GA_ERR любого канала)\n};",
           "  ST_COMMON_BIT_GA_SENSOR_FAIL,                  // Отказ сенсора ГА (бит 6 ST_GA_ERR любого канала)\n"
           "  ST_COMMON_BIT_SOS_GROUP,                       // SOS группы (от сервера, бит SOS_GROUP ControlLora)\n};")
    print("[+] shared_types: +ST_COMMON_BIT_SOS_GROUP")
else: print("[=] shared_types: SOS_GROUP уже есть")

# ---- 1b. Alert_vector.h: +ALERT_SOS_GROUP ----
if "ALERT_SOS_GROUP" not in rd(AH):
    rep(AH,"  ALERT_SOS,              /* SOS (бит ST_COMMON_BIT_SOS в StateErr)         */\n",
           "  ALERT_SOS,              /* SOS свой (кнопка, бит SOS в StateErr)        */\n"
           "  ALERT_SOS_GROUP,        /* SOS группы (от сервера, бит SOS_GROUP)       */\n")
    print("[+] Alert_vector.h: +ALERT_SOS_GROUP")
else: print("[=] Alert_vector.h: SOS_GROUP уже есть")

# ---- 1c. Alert_vector.c: s_desc +SOS_GROUP; status_bits_tick +SOS_GROUP ----
a=rd(AV)
if "[ALERT_SOS_GROUP]" not in a:
    rep(AV,"""  [ALERT_SOS] = {
    .name = "SOS", .prio = ALERT_PRIO_ALARM, .ch = ALERT_CHMASK_ALARM,
    .blink = 1u, .ack_button = 0u, .burst = 0u,
    .period_ms = 600u, .pause_ms = 400u,
    .voice = ALERT_SND_NONE, .hold_s = 0u },
""",
"""  [ALERT_SOS] = {
    .name = "SOS", .prio = ALERT_PRIO_ALARM, .ch = ALERT_CHMASK_ALARM,
    .blink = 1u, .ack_button = 0u, .burst = 0u,
    .period_ms = 600u, .pause_ms = 400u,
    .voice = ALERT_SND_NONE, .hold_s = 0u },
  /* SOS группы - флаг сервера «в группе кто-то нажал SOS» (бит SOS_GROUP).   */
  [ALERT_SOS_GROUP] = {
    .name = "SOS группы", .prio = ALERT_PRIO_ALARM, .ch = ALERT_CHMASK_ALARM,
    .blink = 1u, .ack_button = 0u, .burst = 0u,
    .period_ms = 600u, .pause_ms = 400u,
    .voice = ALERT_SND_NONE, .hold_s = 0u },
""")
    rep(AV,"""  if (TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_SOS)) { Alert_Set(ALERT_SOS); }
  else                                               { Alert_Clear(ALERT_SOS); }
""",
"""  if (TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_SOS)) { Alert_Set(ALERT_SOS); }
  else                                               { Alert_Clear(ALERT_SOS); }
  if (TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_SOS_GROUP)) { Alert_Set(ALERT_SOS_GROUP); }
  else                                                     { Alert_Clear(ALERT_SOS_GROUP); }
""")
    print("[+] Alert_vector.c: s_desc/status_bits_tick +SOS_GROUP")
else: print("[=] Alert_vector.c: SOS_GROUP уже есть")

# ---- 1d. Lora: зеркало бита SOS_GROUP ----
l=rd(LO)
if "ST_COMMON_BIT_SOS_GROUP" not in l:
    rep(LO,"""        if (TESTBIT(lora.ControlLora, ST_LORA_BIT_EVACUATION)) {
            SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_EVACUATION);
        }""",
"""        if (TESTBIT(lora.ControlLora, ST_LORA_BIT_SOS_GROUP)) {
            SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_SOS_GROUP);
        }
        else {
            CLEAR_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_SOS_GROUP);
        }
        if (TESTBIT(lora.ControlLora, ST_LORA_BIT_EVACUATION)) {
            SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_EVACUATION);
        }""")
    print("[+] Lora: зеркало SOS_GROUP -> StateErr")
else: print("[=] Lora: SOS_GROUP уже есть")

# ---- 2. газ: hold_s=0 + снятие ДРУГИМ статусом ----
a=rd(AV)
if ".hold_s = ALERT_GAS_HOLD_S }," in a:
    rep(AV,"    .voice = ALERT_SND_GAS, .hold_s = ALERT_GAS_HOLD_S },",
           "    .voice = ALERT_SND_GAS, .hold_s = 0u },   /* газ не гаснет по таймауту */")
    rep(AV,"""  if (msg_contains(text_cp1251, len, ALERT_MSG_MARK_EVACUATION) != 0u)
  {""",
"""  /* Сюда доходим только с НЕ-газовым сообщением: «ГАЗ» от сервера живёт,
     пока не придёт ДРУГОЙ статус (требование автора) - снимаем серверный
     источник; локальный (пороги Avis) снимается только возвратом в норму.   */
  if ((s_gas_src & ALERT_GAS_SRC_SERVER) != 0u)
  {
    s_gas_src = (uint8_t)(s_gas_src & (uint8_t)~ALERT_GAS_SRC_SERVER);
    if (s_gas_src == 0u) { Alert_Clear(ALERT_GAS_ALARM); }
  }
  if (msg_contains(text_cp1251, len, ALERT_MSG_MARK_EVACUATION) != 0u)
  {""")
    print("[+] газ: hold_s=0 + снятие другим статусом")
else: print("[=] газ: уже без hold_s")

# ---- 3a. Menu_vector.h: экраны отсчёта ----
if "SCREEN_COUNTDOWN_SHUTDOWN" not in rd(MH):
    rep(MH,"  SCREEN_TEXT_MESSAGE,\n  //------------------------------------------------------------------------------\n  SCREEN_COUNT",
           "  SCREEN_TEXT_MESSAGE,\n  //------------------------------------------------------------------------------\n"
           "  SCREEN_COUNTDOWN_SHUTDOWN,  /* обратный отсчёт выключения (удержание) */\n"
           "  SCREEN_COUNTDOWN_SOS,       /* обратный отсчёт SOS (удержание)        */\n\n"
           "  SCREEN_COUNT")
    print("[+] Menu_vector.h: +SCREEN_COUNTDOWN_*")
else: print("[=] Menu_vector.h: экраны уже есть")

# ---- 3b. TFT scr_map: экраны отсчёта -> ui_ScreenHeadMain ----
t=rd(TI)
if "SCREEN_COUNTDOWN_SHUTDOWN" not in t:
    rep(TI,"static const screen_map_t scr_map[8] = {","static const screen_map_t scr_map[] = {")
    rep(TI,"\t{SCREEN_TEXT_MESSAGE,   &ui_ScreenMenu,     ui_ScreenMenu_screen_init},\n};",
           "\t{SCREEN_TEXT_MESSAGE,   &ui_ScreenMenu,     ui_ScreenMenu_screen_init},\n"
           "\t{SCREEN_COUNTDOWN_SHUTDOWN, &ui_ScreenHeadMain, ui_ScreenHeadMain_screen_init},\n"
           "\t{SCREEN_COUNTDOWN_SOS,      &ui_ScreenHeadMain, ui_ScreenHeadMain_screen_init},\n};")
    print("[+] TFT scr_map: +экраны отсчёта (HeadMain)")
else: print("[=] TFT scr_map: уже есть")

# ---- 3c. Menu_Draw: кейсы отсчёта вызывают LCD_* ----
m=rd(MV)
if "case SCREEN_COUNTDOWN_SHUTDOWN:" not in m:
    rep(MV,"""    default:   /* START/CONNECT/MESSAGE - экраны автора (ждут отрисовку) */
      break;""",
"""    case SCREEN_COUNTDOWN_SHUTDOWN:
      LCD_CountdownToShutdown(((uint32_t)TEST_TIMER_RUN(TIMER_SHUTDOWN) + 999u) / 1000u);
      break;
    case SCREEN_COUNTDOWN_SOS:
      LCD_CountdownToSOS(((uint32_t)TEST_TIMER_RUN(TIMER_PRESS_SOS) + 999u) / 1000u,
                         (TEST_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_SOS) != 0u) ? 1u : 0u);
      break;
    default:   /* START/CONNECT/MESSAGE - экраны автора (ждут отрисовку) */
      break;""")
    print("[+] Menu_Draw: кейсы SCREEN_COUNTDOWN_*")
else: print("[=] Menu_Draw: кейсы уже есть")

# ---- 3d. машина отсчёта: переходы на экраны, убрать Menu_CountdownDraw ----
m=rd(MV)
if "s_cnt_return" not in m:
    rep(MV,"static uint8_t s_cnt_kind = 0u;   /* 0 = нет, 1 = выключение, 2 = SOS */",
           "static uint8_t s_cnt_kind = 0u;   /* 0 = нет, 1 = выключение, 2 = SOS */\n"
           "static uint8_t s_cnt_return = 0u; /* экран возврата после отсчёта */")
    rep(MV,"""  if ((device_turn == DEVICE_TURNED_OFF) || (s_cnt_kind != 0u)) { return; }
  s_cnt_kind = 1u;
  START_TIMER(TIMER_SHUTDOWN, (uint16_t)VECTOR_COUNTDOWN_SHUTDOWN_MS);
  Alert_Set(ALERT_SHUTDOWN);""",
"""  if ((device_turn == DEVICE_TURNED_OFF) || (s_cnt_kind != 0u)) { return; }
  s_cnt_return = menu_status.screen;
  s_cnt_kind = 1u;
  START_TIMER(TIMER_SHUTDOWN, (uint16_t)VECTOR_COUNTDOWN_SHUTDOWN_MS);
  Alert_Set(ALERT_SHUTDOWN);
  Menu_GoTo((uint8_t)SCREEN_COUNTDOWN_SHUTDOWN);""")
    rep(MV,"""    LOG_I(VLOG_M_SYS, "menu: countdown cancelled (button released)");
    countdown_cancel();
    return;""",
"""    LOG_I(VLOG_M_SYS, "menu: countdown cancelled (button released)");
    countdown_cancel();
    Menu_GoTo(s_cnt_return);
    return;""")
    rep(MV,"""    Alert_Set(ALERT_SOS);
    Menu_ResetButtons();
    LOG_W(VLOG_M_SYS, "menu: SOS set by hold countdown");""",
"""    Alert_Set(ALERT_SOS);
    Menu_ResetButtons();
    Menu_GoTo(s_cnt_return);
    LOG_W(VLOG_M_SYS, "menu: SOS set by hold countdown");""")
    rep(MV,"""      s_cnt_kind = 2u;
      START_TIMER(TIMER_PRESS_SOS, (uint16_t)VECTOR_COUNTDOWN_SOS_MS);
      Alert_Set(ALERT_SOS_COUNTDOWN);""",
"""      s_cnt_return = menu_status.screen;
      s_cnt_kind = 2u;
      START_TIMER(TIMER_PRESS_SOS, (uint16_t)VECTOR_COUNTDOWN_SOS_MS);
      Alert_Set(ALERT_SOS_COUNTDOWN);
      Menu_GoTo((uint8_t)SCREEN_COUNTDOWN_SOS);""")
    m=rd(MV)
    m=re.sub(r'/\* Цифра отсчёта \(после Menu_Draw.*?static void Menu_CountdownDraw\(void\).*?\n\}\n', '', m, flags=re.DOTALL)
    m=m.replace("  Menu_Draw();\n  Menu_CountdownDraw();\n}","  Menu_Draw();\n}")
    wr(MV,m)
    print("[+] машина отсчёта: экраны + возврат, Menu_CountdownDraw убран")
else: print("[=] машина отсчёта: уже на экранах")

print("ГОТОВО. Собери проект (Debug).")