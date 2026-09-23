/**
  ******************************************************************************
  * @file    audio_player.c
  * @brief   Плеер звуков из внешней SPI flash (очередь, состояния, громкость)
  *
  *          Поток-владелец воспроизведения: только он трогает SAI/DMA.
  *          Команды приходят через tx_queue, пробуждение - через семафор,
  *          который взводят и команды, и ISR завершения DMA.
  *
  *          Тестовый вход - кнопки:
  *            BUTTON1 (PB1) - цикл состояний 0 -> 1 -> 2 -> 0
  *                            st0 = звук #0, st1 = звук #1, st2 = ТИШИНА
  *            BUTTON2 (PB2) - проиграть следующий звук образа по очереди
  *            BUTTON3 (PB3) - стоп
  ******************************************************************************
  */
#include "audio_player.h"
#include "audio_image.h"
#include "spiflash.h"
#include "extstore.h"
#include "audio_beep.h"
#include "sai.h"
#include "main.h"
#include "tx_api.h"

/* ------------------------------------------------------------------ RTOS -- */
#define AP_STACK_SIZE   4096u
#define AP_PRIORITY     10u

static TX_THREAD    ap_thread;
static uint8_t      ap_stack[AP_STACK_SIZE] __attribute__((aligned(8)));
static TX_QUEUE     ap_queue;
static ULONG        ap_queue_mem[AUDIO_QUEUE_LEN];
static TX_SEMAPHORE ap_wake;

/* Команды */
#define CMD_PLAY    1u
#define CMD_STOP    2u
#define CMD_STATE   3u
#define CMD_BEEP    4u
#define MSG(cmd, arg)   ((((ULONG)(cmd)) << 16) | ((ULONG)(arg) & 0xFFFFu))

/* ---------------------------------------------------------------- образ --- */
#define AP_MAX_SOUNDS   32u
static audio_img_header_t ap_hdr;
static audio_img_entry_t  ap_tab[AP_MAX_SOUNDS];
static uint8_t            ap_img_ok = 0;

/* ---------------------------------------------------------------- буфер --- */
static int16_t ap_buf[AUDIO_BUF_SAMPLES];

/* -------------------------------------------------------------- состояние - */
static volatile uint8_t  ap_state = 0;
static const uint16_t    ap_state_map[3] = { 0u, 1u, AUDIO_STATE_SILENT };

/* --------------------------------------------------------------- громкость */
static volatile int32_t ap_vol_q15 = 32767;   /* 100% */

/* ---------------------------------------------------------- воспроизведение */
static volatile int32_t ap_playing_idx = -1;

/* ---------------------------------------------------------------- отладка -- */
volatile uint32_t audio_dbg_played    = 0;
volatile uint32_t audio_dbg_started   = 0;
volatile uint32_t audio_dbg_errors    = 0;
volatile int32_t  audio_dbg_cur_idx   = -1;
volatile uint32_t audio_dbg_last_err  = 0;

/* ============================================================ внутреннее == */
static void apply_volume(int16_t *p, uint32_t n)
{
  int32_t g = ap_vol_q15;
  uint32_t i;
  if (g >= 32767)
  {
    return;
  }
  for (i = 0; i < n; i++)
  {
    int32_t v = ((int32_t)p[i] * g) >> 15;
    if (v > 32767)      { v = 32767;  }
    else if (v < -32768){ v = -32768; }
    p[i] = (int16_t)v;
  }
}

/* Аварийный писк: DMA прямо из const-массива во внутренней flash */
static audio_err_t beep_now(void)
{
  if (HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)audio_beep_pcm,
                           (uint16_t)audio_beep_samples) != HAL_OK)
  {
    audio_dbg_errors++;
    audio_dbg_last_err = (uint32_t)AUDIO_ERR_IO;
    return AUDIO_ERR_IO;
  }
  ap_playing_idx    = -2;      /* маркер: играет писк */
  audio_dbg_cur_idx = -2;
  audio_dbg_started++;
  return AUDIO_OK;
}

static audio_err_t start_now(uint16_t idx)
{
  const audio_img_entry_t *e;
  audio_err_t rc = AUDIO_OK;

  if (!ap_img_ok)
  {
    /* Внешняя flash мертва или образ битый: аварийный писк из внутренней
       flash, чтобы устройство не молчало совсем. */
    audio_dbg_last_err = (uint32_t)AUDIO_ERR_NO_IMAGE;
    return beep_now();
  }
  else if (idx >= ap_hdr.count)      { rc = AUDIO_ERR_BAD_INDEX; }
  else
  {
    e = &ap_tab[idx];
    if ((e->length / 2u) > AUDIO_BUF_SAMPLES)
    {
      rc = AUDIO_ERR_TOO_LONG;
    }
    else if (ext_read(AUDIO_IMG_BASE_ADDR + e->offset, (uint8_t *)ap_buf, e->length) != 0)
    {
      rc = AUDIO_ERR_IO;
    }
    else
    {
      apply_volume(ap_buf, e->length / 2u);
      if (HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)ap_buf,
                               (uint16_t)(e->length / 2u)) != HAL_OK)
      {
        rc = AUDIO_ERR_IO;
      }
      else
      {
        ap_playing_idx     = (int32_t)idx;
        audio_dbg_cur_idx  = (int32_t)idx;
        audio_dbg_started++;
      }
    }
  }

  if (rc != AUDIO_OK)
  {
    audio_dbg_errors++;
    audio_dbg_last_err = (uint32_t)rc;
  }
  return rc;
}

static void stop_now(void)
{
  if (ap_playing_idx != -1)
  {
    (void)HAL_SAI_Abort(&hsai_BlockA1);
    ap_playing_idx    = -1;
    audio_dbg_cur_idx = -1;
  }
}

static void queue_clear(void)
{
  (void)tx_queue_flush(&ap_queue);
}

/* --------------------------------------------------------------- поток ---- */
static void ap_thread_entry(ULONG arg)
{
  (void)arg;

  for (;;)
  {
    ULONG msg = 0;

    /* Ждём любого события: команда ИЛИ завершение DMA */
    (void)tx_semaphore_get(&ap_wake, TX_WAIT_FOREVER);

    /* 1. Разбираем ВСЕ накопившиеся команды */
    while (tx_queue_receive(&ap_queue, &msg, TX_NO_WAIT) == TX_SUCCESS)
    {
      ULONG cmd = (msg >> 16) & 0xFFu;
      ULONG a   = msg & 0xFFFFu;

      if (cmd == CMD_STOP)
      {
        stop_now();
        queue_clear();
      }
      else if (cmd == CMD_STATE)
      {
        uint16_t snd;
        stop_now();
        queue_clear();
        ap_state = (uint8_t)a;
        snd = (a < (sizeof ap_state_map / sizeof ap_state_map[0]))
              ? ap_state_map[a] : AUDIO_STATE_SILENT;
        if (snd != AUDIO_STATE_SILENT)
        {
          (void)start_now(snd);       /* смена режима прерывает текущий звук */
        }
      }
      else if (cmd == CMD_BEEP)
      {
        stop_now();
        queue_clear();
        (void)beep_now();
      }
      else if (cmd == CMD_PLAY)
      {
        if (ap_playing_idx == -1)
        {
          (void)start_now((uint16_t)a);   /* свободно - играем сразу        */
        }
        else
        {
          /* занято: текущий доигрывает до конца, новый ждёт в очереди      */
          if (tx_queue_send(&ap_queue, &msg, TX_NO_WAIT) != TX_SUCCESS)
          {
            audio_dbg_last_err = (uint32_t)AUDIO_ERR_QUEUE_FULL;
          }
          /* вернули сообщение обратно в очередь - выйдем из drain-цикла,
             чтобы не крутиться: очередь непустая обработается ниже */
          break;
        }
      }
      else
      {
        /* неизвестная команда - игнорируем */
      }
    }

    /* 2. Если освободились и в очереди что-то стоит - берём следующее */
    if (ap_playing_idx == -1)
    {
      ULONG next = 0;
      if (tx_queue_receive(&ap_queue, &next, TX_NO_WAIT) == TX_SUCCESS)
      {
        (void)start_now((uint16_t)(next & 0xFFFFu));
      }
    }
  }
}

/* =============================================================== публичное */
void audio_init(void)
{
  uint8_t  raw[AUDIO_IMG_HEADER_SIZE + AP_MAX_SOUNDS * AUDIO_IMG_ENTRY_SIZE];
  uint32_t tbl_bytes;

  /* --- читаем и проверяем образ --- */
  if (sf_read(AUDIO_IMG_BASE_ADDR, raw, sizeof ap_hdr) == HAL_OK)
  {
    const audio_img_header_t *h = (const audio_img_header_t *)raw;
    if ((h->magic == AUDIO_IMG_MAGIC) && (h->version == AUDIO_IMG_VERSION) &&
        (h->count > 0u) && (h->count <= AP_MAX_SOUNDS))
    {
      tbl_bytes = (uint32_t)h->count * AUDIO_IMG_ENTRY_SIZE;
      if (sf_read(AUDIO_IMG_BASE_ADDR + AUDIO_IMG_HEADER_SIZE, raw, tbl_bytes) == HAL_OK)
      {
        uint32_t crc = 0;
        uint32_t i;
        /* crc32 таблицы считаем тем же алгоритмом, что и pack_sounds.py
           (zlib.crc32); здесь - простая побайтовая реализация без таблицы */
        crc = 0xFFFFFFFFu;
        for (i = 0; i < tbl_bytes; i++)
        {
          uint32_t b = raw[i];
          int k;
          crc ^= b;
          for (k = 0; k < 8; k++)
          {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
          }
        }
        crc ^= 0xFFFFFFFFu;

        if (crc == h->table_crc32)
        {
          ap_hdr = *h;
          for (i = 0; i < h->count; i++)
          {
            const uint8_t *p = raw + i * AUDIO_IMG_ENTRY_SIZE;
            audio_img_entry_t *e = &ap_tab[i];
            e->offset   = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                          ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            e->length   = (uint32_t)p[4] | ((uint32_t)p[5] << 8) |
                          ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
            e->samples  = (uint32_t)p[8] | ((uint32_t)p[9] << 8) |
                          ((uint32_t)p[10] << 16) | ((uint32_t)p[11] << 24);
            e->rate     = (uint16_t)((uint32_t)p[12] | ((uint32_t)p[13] << 8));
            e->channels = p[14];
            e->format   = p[15];
            e->crc32    = (uint32_t)p[16] | ((uint32_t)p[17] << 8) |
                          ((uint32_t)p[18] << 16) | ((uint32_t)p[19] << 24);
            for (uint32_t j = 0; j < AUDIO_IMG_NAME_LEN; j++)
            {
              ((char *)&e->name)[j] = (char)p[20 + j];
            }
            ap_img_ok = 1;
          }
        }
      }
    }
  }

  /* --- RTOS-объекты и поток (создаются из tx_application_define) --- */
  (void)tx_semaphore_create(&ap_wake, "audio wake", 0);
  (void)tx_queue_create(&ap_queue, "audio cmd", TX_1_ULONG,
                        ap_queue_mem, sizeof ap_queue_mem);
  (void)tx_thread_create(&ap_thread, "Audio Player", ap_thread_entry, 0,
                         ap_stack, AP_STACK_SIZE,
                         AP_PRIORITY, AP_PRIORITY, TX_NO_TIME_SLICE, TX_AUTO_START);
}

audio_err_t audio_play(uint16_t idx)
{
  ULONG msg = MSG(CMD_PLAY, idx);
  if (tx_queue_send(&ap_queue, &msg, TX_NO_WAIT) != TX_SUCCESS)
  {
    return AUDIO_ERR_QUEUE_FULL;
  }
  (void)tx_semaphore_put(&ap_wake);
  return AUDIO_OK;
}

audio_err_t audio_play_name(const char *name)
{
  uint32_t i;
  if (!ap_img_ok)
  {
    return AUDIO_ERR_NO_IMAGE;
  }
  for (i = 0; i < ap_hdr.count; i++)
  {
    uint32_t j;
    int match = 1;
    for (j = 0; j < AUDIO_IMG_NAME_LEN; j++)
    {
      char c = ap_tab[i].name[j];
      char n = name[j];
      if (c == '\0' && n == '\0') { break; }
      if (c != n) { match = 0; break; }
    }
    if (match)
    {
      return audio_play((uint16_t)i);
    }
  }
  return AUDIO_ERR_BAD_INDEX;
}

void audio_beep(void)
{
  ULONG msg = MSG(CMD_BEEP, 0);
  if (tx_queue_send(&ap_queue, &msg, TX_NO_WAIT) == TX_SUCCESS)
  {
    (void)tx_semaphore_put(&ap_wake);
  }
}

void audio_stop(void)
{
  ULONG msg = MSG(CMD_STOP, 0);
  if (tx_queue_send(&ap_queue, &msg, TX_NO_WAIT) == TX_SUCCESS)
  {
    (void)tx_semaphore_put(&ap_wake);
  }
}

void audio_set_state(uint8_t st)
{
  ULONG msg = MSG(CMD_STATE, st);
  if (tx_queue_send(&ap_queue, &msg, TX_NO_WAIT) == TX_SUCCESS)
  {
    (void)tx_semaphore_put(&ap_wake);
  }
}

uint8_t audio_get_state(void) { return ap_state; }

void audio_set_volume(uint8_t percent)
{
  uint32_t p = (percent > 100u) ? 100u : percent;
  ap_vol_q15 = (int32_t)((p * 32767u) / 100u);
}

uint8_t audio_get_volume(void)
{
  return (uint8_t)(((uint32_t)ap_vol_q15 * 100u) / 32767u);
}

uint8_t  audio_image_ok(void) { return ap_img_ok; }
uint16_t audio_count(void)    { return ap_img_ok ? ap_hdr.count : 0u; }

const char *audio_name(uint16_t idx)
{
  if (!ap_img_ok || (idx >= ap_hdr.count))
  {
    return "";
  }
  return ap_tab[idx].name;
}

/* ================================================== колбэки HAL SAI (ISR) == */
void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_A)
  {
    if (ap_playing_idx != -1)
    {
      audio_dbg_played++;
      ap_playing_idx    = -1;
      audio_dbg_cur_idx = -1;
      (void)tx_semaphore_put(&ap_wake);
    }
  }
}

void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_A)
  {
    audio_dbg_errors++;
    audio_dbg_last_err = 0x1000u | hsai->ErrorCode;
    if (ap_playing_idx != -1)
    {
      ap_playing_idx    = -1;
      audio_dbg_cur_idx = -1;
      (void)tx_semaphore_put(&ap_wake);
    }
  }
}

/* ============================================== тестовый вход: кнопки (ISR) = */
static uint32_t ap_btn_last = 0;
static uint8_t  ap_test_next_idx = 0;

void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
  uint32_t now = HAL_GetTick();

  /* антидребезг: игнорируем повторы чаще 200 мс */
  if ((now - ap_btn_last) < 200u)
  {
    return;
  }
  ap_btn_last = now;

  if (GPIO_Pin == BUTTON1_Pin)
  {
    /* цикл состояний: 0 (звук#0) -> 1 (звук#1) -> 2 (ТИШИНА) -> 0 */
    audio_set_state((uint8_t)((audio_get_state() + 1u) % 3u));
  }
  else if (GPIO_Pin == BUTTON2_Pin)
  {
    uint16_t n = audio_count();
    if (n > 0u)
    {
      (void)audio_play(ap_test_next_idx);
      ap_test_next_idx = (uint8_t)((ap_test_next_idx + 1u) % n);
    }
  }
  else if (GPIO_Pin == BUTTON3_Pin)
  {
    audio_stop();
  }
  else
  {
    /* прочие EXTI - не наши */
  }
}
