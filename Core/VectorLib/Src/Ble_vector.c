/**
  ******************************************************************************
  * @file    Ble_vector.c
  * @brief   BLE: ответная сторона протокола Avis (v26) - реализация
  *
  *          Машина приёма: байты из InputBuffer[TYPE_BLE] (пишет ISR USART3)
  *          набираются в строку до 0x0D (или до таймаута тишины в Ble_Run).
  *          Полная строка разбирается: "AT" -> OK; "AT+<CMD>[=<payload>]" ->
  *          поиск в ble_cmds[]:
  *            ADVMSDDATAGET - собрать str_ble_vector_data_t из наших данных и
  *                            отдать hex-строкой + OK (мастер забирает их в
  *                            своё объявление/опрос);
  *            ADVMSDDATASET=<hex> - декодировать в str_ble_data_set_t (формат
  *                            Ble_mk01) -> Ble_ApplyConnectedDevice() ->
  *                            Connected_device -> OK/ERROR;
  *            MODINFO?      - строка версий + OK;
  *            прочее        - ERROR + cnt_unknown_cmd.
  *          Протокол дополняется (автор): новая команда = одна строка в
  *          ble_cmds[] + ветка в ble_handle_line().
  *
  *          Ответы уходят transmit_buffer(..., TYPE_BLE) (buffer.c -> USART_BLE
  *          = huart3, main.h). Всё НЕБЛОКИРУЮЩЕ: один проход Receiver Task
  *          разбирает столько байт, сколько есть в кольце.
  *
  *          КОНТЕКСТ: Ble_Receive() - Receiver Task (~10 мс); Ble_Run() -
  *          Measure Task (1 с). Strong-символы замещают weak-заглушки
  *          Vector_main.c (правило проекта - без его правки).
  ******************************************************************************
  */
#include "Vector_main.h"     /* SNS_CFG, sensors_status, макросы статусов, лог */
#include "Ble_vector.h"
#include "buffer.h"          /* InputBuffer/receive_buffer/transmit_buffer/TYPE_BLE */
#include "vector_tick.h"
#include "vector_log.h"
#include <string.h>
#include <stdio.h>    /* sprintf: строка MODINFO */

#if CONFIG_BLE

volatile ble_vector_status_t ble_status = {0};
str_ble_data_set_t  ble_rx_data_set;      /* последний принятый набор мастера */

/* Строковые константы протокола (как в Ble_mk01.h). */
#define BLE_RSP_OK        "OK"
#define BLE_RSP_ERROR     "ERROR"
#define BLE_TERM          (0x0Du)

/* --- таблица команд: протокол будет дополняться - добавлять сюда ----------- */
typedef struct
{
  const char *name;      /* имя после "AT+" (без '=' и payload)  */
  uint8_t     id;        /* ble_cmd_t                            */
  uint8_t     need_data; /* 1 = ожидает "=<hex payload>"         */
} ble_cmd_entry_t;

static const ble_cmd_entry_t ble_cmds[] =
{
  { "ADVMSDDATAGET", BLE_CMD_ADVMSDDATAGET, 0u },
  { "ADVMSDDATASET", BLE_CMD_ADVMSDDATASET, 1u },
  { "MODINFO?",      BLE_CMD_MODINFO,       0u },
};
#define BLE_CMDS_COUNT  (sizeof(ble_cmds) / sizeof(ble_cmds[0]))

/* Буферы модуля (статика: владелец один - Receiver Task). */
static char     ble_line[VECTOR_BLE_MAX_LINE];  /* набираемая строка (без 0x0D) */
static uint8_t  ble_tx[VECTOR_BLE_MAX_LINE + 8u];

/* ============================================================================
 *                            низкоуровневые хелперы
 * ============================================================================ */

/* Отправить строку ответа с терминатором 0x0D. КОНТЕКСТ: Receiver Task. */
static void ble_send_line(const char *s)
{
  uint16_t n = (uint16_t)strlen(s);
  if (n > (VECTOR_BLE_MAX_LINE - 1u))
  {
    n = (VECTOR_BLE_MAX_LINE - 1u);
  }
  memcpy(ble_tx, s, n);
  ble_tx[n] = BLE_TERM;
  transmit_buffer(ble_tx, (uint16_t)(n + 1u), TYPE_BLE);
}

/* Отправить N байт hex-строкой ("%02x" побайтно, как payload в Ble_mk01) +
   терминатор, затем отдельной строкой "OK". КОНТЕКСТ: Receiver Task.       */
static void ble_send_hex_ok(const uint8_t *data, uint16_t len)
{
  static const char hex[] = "0123456789abcdef";
  uint16_t i;
  uint16_t p = 0;

  if ((uint32_t)len * 2u > VECTOR_BLE_MAX_LINE)
  {
    len = (uint16_t)(VECTOR_BLE_MAX_LINE / 2u);
  }
  for (i = 0; i < len; i++)
  {
    ble_tx[p++] = (uint8_t)hex[data[i] >> 4];
    ble_tx[p++] = (uint8_t)hex[data[i] & 0x0Fu];
  }
  ble_tx[p++] = BLE_TERM;
  transmit_buffer(ble_tx, p, TYPE_BLE);
  ble_send_line(BLE_RSP_OK);
}

/* Декодировать hex-строку в буфер. Возврат: число байт, -1 = ошибка формата
   (нечётная длина / не-hex символ / не влезает). КОНТЕКСТ: любой.          */
static int ble_hex_decode(const char *hexstr, uint16_t hexlen, uint8_t *out, uint16_t out_max)
{
  uint16_t i;
  uint16_t n = 0;

  if ((hexlen & 1u) != 0u)
  {
    return -1;
  }
  for (i = 0; i < hexlen; i += 2u)
  {
    int hi, lo;
    char c = hexstr[i];
    if ((c >= '0') && (c <= '9'))      { hi = c - '0'; }
    else if ((c >= 'a') && (c <= 'f')) { hi = c - 'a' + 10; }
    else if ((c >= 'A') && (c <= 'F')) { hi = c - 'A' + 10; }
    else                               { return -1; }
    c = hexstr[i + 1u];
    if ((c >= '0') && (c <= '9'))      { lo = c - '0'; }
    else if ((c >= 'a') && (c <= 'f')) { lo = c - 'a' + 10; }
    else if ((c >= 'A') && (c <= 'F')) { lo = c - 'A' + 10; }
    else                               { return -1; }
    if (n >= out_max)
    {
      return -1;
    }
    out[n++] = (uint8_t)((hi << 4) | lo);
  }
  return (int)n;
}

/* ============================================================================
 *                          наполнение ответа и приём
 * ============================================================================ */

/* Собрать НАШИ данные для ответа на ADVMSDDATAGET. Заголовок байт-в-байт как
   str_ble_data_set_t (парсер мастера примет); наполнение - из sensors_status
   и Sns_Cfg_struct. Расширения протокола (автор) добавлять ЗДЕСЬ и в типе
   str_ble_vector_data_t. КОНТЕКСТ: Receiver Task.                          */
static uint16_t ble_fill_answer(str_ble_vector_data_t *out)
{
  out->data_struct_ver = DATA_STRUCT_VER;
  out->dev_id          = (uint8_t)VECTOR_BLE_DEV_ID;
  out->dev_hw_ver      = (uint8_t)Sns_Cfg_struct.Config_common.HardwareVersion;
  out->dev_fw_ver      = (uint8_t)FIRMWARE_VERSION;
  out->charge          = (uint8_t)sensors_status.battery_charge_percent;
  out->sensors_enabled = 0u;   /* каналов измерения пока нет (SNS_Dev_Type в
                                  резерве) - появятся: заполнять отсюда      */
  out->rfu             = 0u;
  return (uint16_t)sizeof(str_ble_vector_data_t);
}

/* Разложить принятый набор мастера в Connected_device (SNS_Dev_Type).
   Возврат: число разложенных каналов. КОНТЕКСТ: Receiver Task.             */
uint8_t Ble_ApplyConnectedDevice(const str_ble_data_set_t *set)
{
  uint8_t n = set->sensors_enabled;
  uint8_t i;

  if (n > COUNT_CHAN_BLE)
  {
    n = (uint8_t)COUNT_CHAN_BLE;
  }
  if (n > COUNT_CHAN)
  {
    n = (uint8_t)COUNT_CHAN;
  }

  Connected_device.dev_id                 = set->dev_id;
  Connected_device.HardwareVersion        = set->dev_hw_ver;
  Connected_device.FirmwareVersion        = set->dev_fw_ver;
  Connected_device.battery_charge_percent = set->charge;

  for (i = 0; i < n; i++)
  {
    uint16_t raw = (uint16_t)set->sensors[i].Concentration[0] |
                   ((uint16_t)set->sensors[i].Concentration[1] << 8);
    Connected_device.Sensor[i].channel          = i;
    Connected_device.Sensor[i].num_formula_gas  = set->sensors[i].Type_gas;
    /* raw-концентрация как пришла (дискретность/коэффициент мастера неизвестны -
       уточнение протокола у автора; при появлении - домножать здесь).        */
    Connected_device.Sensor[i].ConcentrationVal = (float)raw;
    Connected_device.Sensor[i].StateErr         = set->sensors[i].State_channel;
    Connected_device.Sensor[i].Setting          = set->sensors[i].Setting_channel;
  }
  /* Общий статус устройства мастер не передаёт - составим из ошибок каналов
     (OR). State у Connected_device - наш агрегат.                            */
  {
    uint32_t st = 0;
    for (i = 0; i < n; i++)
    {
      st |= Connected_device.Sensor[i].StateErr;
    }
    Connected_device.StateErr = st;
  }
  return n;
}

/* ============================================================================
 *                         разбор полной строки
 * ============================================================================ */
static void ble_handle_line(void)
{
  uint16_t len = (uint16_t)strlen(ble_line);
  const char *payload = (const char *)0;
  uint8_t cmd = BLE_CMD_NONE;
  uint16_t i;

  ble_status.cnt_rx_lines++;
  ble_status.last_cmd = BLE_CMD_NONE;

  if (len == 0u)
  {
    return;                                   /* пустая строка - игнор */
  }

  /* все команды начинаются с "AT" */
  if ((ble_line[0] != 'A') || (ble_line[1] != 'T'))
  {
    ble_status.cnt_unknown_cmd++;
    ble_send_line(BLE_RSP_ERROR);
    ble_status.cnt_tx_error++;
    return;
  }
  if (len == 2u)                              /* "AT" - пинг */
  {
    ble_status.last_cmd = BLE_CMD_AT;
    ble_send_line(BLE_RSP_OK);
    ble_status.cnt_tx_ok++;
    return;
  }
  if (ble_line[2] != '+')
  {
    ble_status.cnt_unknown_cmd++;
    ble_send_line(BLE_RSP_ERROR);
    ble_status.cnt_tx_error++;
    return;
  }

  /* "AT+CMD[=payload]": найти '=' и поискать имя в таблице */
  {
    char *eq = strchr(&ble_line[3], '=');
    uint16_t name_len;
    if (eq != (char *)0)
    {
      *eq = '\0';
      payload = eq + 1;
    }
    name_len = (uint16_t)strlen(&ble_line[3]);
    for (i = 0; i < BLE_CMDS_COUNT; i++)
    {
      if ((strlen(ble_cmds[i].name) == name_len) &&
          (strncmp(&ble_line[3], ble_cmds[i].name, name_len) == 0))
      {
        cmd = ble_cmds[i].id;
        if ((ble_cmds[i].need_data != 0u) && (payload == (const char *)0))
        {
          cmd = BLE_CMD_OTHER;                /* команда ждёт payload, его нет */
        }
        break;
      }
    }
  }
  ble_status.last_cmd = cmd;

  switch (cmd)
  {
    case BLE_CMD_ADVMSDDATAGET:
    {
      str_ble_vector_data_t ans;
      uint16_t n = ble_fill_answer(&ans);
      ble_send_hex_ok((const uint8_t *)&ans, n);
      ble_status.cnt_tx_ok++;
      LOG_D(VLOG_M_SYS, "ble: DATAGET -> %u b", (uint32_t)n);
      break;
    }
    case BLE_CMD_ADVMSDDATASET:
    {
      uint8_t raw[sizeof(str_ble_data_set_t)];
      int n = ble_hex_decode(payload, (uint16_t)strlen(payload), raw, (uint16_t)sizeof(raw));
      /* минимальная длина = заголовок набора (без каналов) */
      if ((n < (int)SIZE_STR_BLE_DATA_SET) ||
          (((uint8_t)n - SIZE_STR_BLE_DATA_SET) % SIZE_STR_SENSOR_DATA != 0u))
      {
        ble_status.cnt_decode_fail++;
        ble_send_line(BLE_RSP_ERROR);
        ble_status.cnt_tx_error++;
        LOG_W(VLOG_M_SYS, "ble: DATASET bad len %d", n);
        break;
      }
      memcpy(&ble_rx_data_set, raw, (size_t)n);
      if (ble_rx_data_set.data_struct_ver != DATA_STRUCT_VER)
      {
        LOG_W(VLOG_M_SYS, "ble: DATASET ver %u (want %u)",
              (uint32_t)ble_rx_data_set.data_struct_ver, (uint32_t)DATA_STRUCT_VER);
        /* версию не блокируем: протокол будет дополняться (автор) */
      }
      (void)Ble_ApplyConnectedDevice(&ble_rx_data_set);
      ble_status.cnt_datasets++;
      ble_send_line(BLE_RSP_OK);
      ble_status.cnt_tx_ok++;
      tasks_status.mod[TASK_MOD_BLE].cnt_rx_frames++;
      LOG_I(VLOG_M_SYS, "ble: DATASET ok dev=%u ch=%u",
            (uint32_t)ble_rx_data_set.dev_id,
            (uint32_t)ble_rx_data_set.sensors_enabled);
      break;
    }
    case BLE_CMD_MODINFO:
    {
      char info[64];
      /* состав строки - предварительный (протокол дополняется, автор) */
      (void)sprintf(info, "VECTOR hw%u fw%u",
                    (unsigned)Sns_Cfg_struct.Config_common.HardwareVersion,
                    (unsigned)FIRMWARE_VERSION);
      ble_send_line(info);
      ble_send_line(BLE_RSP_OK);
      ble_status.cnt_tx_ok++;
      break;
    }
    default:                                   /* BLE_CMD_OTHER / неизвестно */
      ble_status.cnt_unknown_cmd++;
      ble_send_line(BLE_RSP_ERROR);
      ble_status.cnt_tx_error++;
      LOG_W(VLOG_M_SYS, "ble: unknown cmd [%s]", ble_line);
      break;
  }
}

/* ============================================================================
 *                              внешние точки
 * ============================================================================ */

/* Приём из кольца TYPE_BLE: побайтовая машина строки. Терминатор 0x0D (как в
   Ble_mk01); 0x0A терминатором не считаем, но и в строку не кладём. Строка
   переполнена -> сброс + ERROR (мастер ретраит 4 раза, как у себя).
   КОНТЕКСТ: Receiver Task, каждый проход (~10 мс), не блокируется.         */
void Ble_Receive(void)
{
  uint8_t b;

  if (TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_BLE) == 0u)
  {
    /* модуль выключен статусом: кольцо всё равно вычитываем, чтобы не
       переполнялось (данные - в discard).                                   */
    while (check_buffer(&InputBuffer[TYPE_BLE]) != 0u)
    {
      if (receive_buffer(&InputBuffer[TYPE_BLE], &b) == 0u)
      {
        break;
      }
    }
    ble_status.busy_line = 0u;
    ble_line[0] = '\0';
    return;
  }

  while (check_buffer(&InputBuffer[TYPE_BLE]) != 0u)
  {
    if (receive_buffer(&InputBuffer[TYPE_BLE], &b) == 0u)
    {
      break;                       /* кольцо опустело между проверкой и чтением */
    }
    tasks_status.mod[TASK_MOD_BLE].cnt_rx_bytes++;
    ble_status.last_rx_ms = VTICK_MS();

    if (b == BLE_TERM)
    {
      if (ble_status.busy_line != 0u)
      {
        ble_line[ble_status.busy_line] = '\0';
        ble_handle_line();
      }
      ble_status.busy_line = 0u;
      ble_line[0] = '\0';
    }
    else if (b == 0x0Au)
    {
      /* игнор (возможная пара к 0x0D) */
    }
    else
    {
      if (ble_status.busy_line < (VECTOR_BLE_MAX_LINE - 1u))
      {
        ble_line[ble_status.busy_line++] = (char)b;
      }
      else
      {
        ble_status.busy_line = 0u;
        ble_line[0] = '\0';
        ble_status.cnt_decode_fail++;
        ble_send_line(BLE_RSP_ERROR);
        ble_status.cnt_tx_error++;
      }
    }
  }
}

/* Периодика (1 с): страховка от потерянного терминатора - если строка
   набирается и тишина дольше VECTOR_BLE_FRAME_TMO_MS, сбросить её.
   КОНТЕКСТ: Measure Task.                                                   */
void Ble_Run(void)
{
  if ((ble_status.busy_line != 0u) &&
      (VTICK_ELAPSED_MS(ble_status.last_rx_ms) > VECTOR_BLE_FRAME_TMO_MS))
  {
    ble_status.busy_line = 0u;
    ble_line[0] = '\0';
    ble_status.cnt_frame_tmo++;
    LOG_W(VLOG_M_SYS, "ble: frame timeout, line dropped");
  }
  tasks_status.mod[TASK_MOD_BLE].last_run_ms = VTICK_MS();
}

#endif /* CONFIG_BLE */
