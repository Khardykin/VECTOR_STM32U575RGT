/**
  ******************************************************************************
  * @file    spiflash.c
  * @brief   Драйвер внешней SPI NOR flash MX25K6435F (SPI1, CS = PA4)
  *
  *          ДВА режима обмена (переключатель VECTOR_SPI_DMA):
  *            - блокирующий ОПРОС: команды, адреса, статус-регистр, короткие
  *              чтения. Там 1..256 байт, настройка DMA вышла бы дороже обмена;
  *            - DMA: большие чтения (подкачка звука в ap_buf). Поток стартует
  *              HAL_SPI_Receive_DMA и СПИТ на семафоре, который взводит ISR.
  *              CPU в это время свободен (LVGL, мост UART), а не крутится в
  *              опросе 13..50 мс на каждом старте звука.
  *          Отказ DMA (таймаут/ошибка) -> это же чтение повторяется ОПРОСОМ с
  *          самого начала, поэтому данные всегда корректны: DMA влияет только
  *          на скорость, не на правильность.
  *
  *          КОНТЕКСТ: поток или инициализация до планировщика. Из ISR НЕ
  *          вызывать - внутри есть ожидание (опрос флага или сон на семафоре).
  *
  *          CS (PA4) держится низким на всю команду. READ = 0x03 + 3 байта
  *          адреса + поток данных произвольной длины (пауза SCK при низком CS
  *          для NOR допустима - этим пользуется разбивка на DMA-куски).
  *
  *          ТРЕБОВАНИЯ К КУБЕ (уже выполнены, см. spi.c/gpdma.c):
  *            SPI1_RX -> GPDMA1_Channel10, включены GPDMA1_Channel10_IRQn И
  *            SPI1_IRQn: HAL_SPI_RxCpltCallback приходит из прерывания EOT
  *            самого SPI, а не из DMA. Без SPI1_IRQn семафор не взведётся.
  ******************************************************************************
  */
#include "spiflash.h"
#include "vector_config.h"
#include "vector_log.h"
#include "vector_tick.h"   /* VTICK_MS(): источник времени выбирается в vector_config.h */
#include "spi.h"
#include "main.h"
#if VECTOR_SPI_DMA
#include "tx_api.h"
#endif

#define SF_TIMEOUT_MS   250u
#define SF_ERASE_TMO_MS 500u
#define SF_PROG_TMO_MS  100u

volatile uint8_t  sf_jedec[3] = { 0, 0, 0 };
volatile uint8_t  sf_rdsr     = 0xFF;
volatile int32_t  sf_probe_rc = -1;

/* ------------------------------------------------------------------ DMA --- */
#if VECTOR_SPI_DMA
/* Кусок одной DMA-транзакции. HAL принимает uint16_t Size, поэтому жёстко
   ограничиваем 65535; VECTOR_SPI_DMA_CHUNK по умолчанию 32768 = 13 мс.     */
#define SF_DMA_CHUNK   ((VECTOR_SPI_DMA_CHUNK > 0xFFFFu) ? 0x8000u : VECTOR_SPI_DMA_CHUNK)
/* Таймаут ожидания куска в тиках ThreadX (TX_TIMER_TICKS_PER_SECOND = 100). */
#define SF_DMA_TMO     (((VECTOR_SPI_DMA_TMO_MS * TX_TIMER_TICKS_PER_SECOND) + 999u) / 1000u)

static TX_SEMAPHORE     sf_dma_sem;        /* взводится из ISR по факту приёма */
static volatile uint8_t sf_dma_ready  = 0; /* семафор создан (sf_dma_init)     */
static volatile uint8_t sf_dma_failed = 0; /* DMA подвела -> до сброса опросом */
static volatile uint8_t sf_dma_err    = 0; /* флаг ошибки текущего куска       */

volatile uint32_t sf_dbg_dma_chunks   = 0;
volatile uint32_t sf_dbg_dma_tmo      = 0;
volatile uint32_t sf_dbg_dma_err      = 0;
volatile uint32_t sf_dbg_dma_fallback = 0;
volatile uint32_t sf_dbg_poll_bytes   = 0;
#else
volatile uint32_t sf_dbg_dma_chunks   = 0;
volatile uint32_t sf_dbg_dma_tmo      = 0;
volatile uint32_t sf_dbg_dma_err      = 0;
volatile uint32_t sf_dbg_dma_fallback = 0;
volatile uint32_t sf_dbg_poll_bytes   = 0;
#endif /* VECTOR_SPI_DMA */

/* ------------------------------------------------------------------ CS --- */
/* Опустить CS (PA4). Начало любой команды. Контекст: поток/инициализация. */
static void cs_low(void)  { HAL_GPIO_WritePin(CS_FLASH_GPIO_Port, CS_FLASH_Pin, GPIO_PIN_RESET); }

/* Поднять CS (PA4). Конец команды: только после этого flash фиксирует
   операцию (для записи/стирания) и выходит из режима потока данных (READ). */
static void cs_high(void) { HAL_GPIO_WritePin(CS_FLASH_GPIO_Port, CS_FLASH_Pin, GPIO_PIN_SET); }

/* Одиночная команда без адреса (WREN, RDID-команда и т.п.).
   Сама управляет CS: низкий на время передачи одного байта, затем высокий.
   Возврат: HAL_OK/HAL_ERROR/HAL_TIMEOUT от HAL_SPI_Transmit.               */
static HAL_StatusTypeDef cmd_only(uint8_t cmd)
{
  HAL_StatusTypeDef st;
  cs_low();
  st = HAL_SPI_Transmit(&hspi1, &cmd, 1, SF_TIMEOUT_MS);
  cs_high();
  return st;
}

/* Команда + 3-байтный адрес (READ/PP/SE). ВНИМАНИЕ: CS остаётся НИЗКИМ,
   вызывающий продолжает обмен данными и сам вызывает cs_high().
   Всегда опросом: 4 байта, DMA здесь только добавила бы задержку.          */
static HAL_StatusTypeDef cmd_addr(uint8_t cmd, uint32_t addr)
{
  uint8_t b[4];
  b[0] = cmd;
  b[1] = (uint8_t)((addr >> 16) & 0xFFu);
  b[2] = (uint8_t)((addr >> 8)  & 0xFFu);
  b[3] = (uint8_t)(addr & 0xFFu);
  cs_low();
  return HAL_SPI_Transmit(&hspi1, b, 4, SF_TIMEOUT_MS);
}

/* Ждёт сброса бита WIP (write in progress) статус-регистра после записи или
   стирания. Опрос RDSR в цикле с общим таймаутом; между опросами поток
   крутится (тут десятки мкс на стирание страницы и десятки мс на сектор).
   Возврат: HAL_OK или HAL_TIMEOUT. CS здесь не удерживается.               */
static HAL_StatusTypeDef wait_busy(uint32_t timeout_ms)
{
  uint32_t t0 = VTICK_MS();
  for (;;)
  {
    uint8_t cmd = SF_CMD_RDSR;
    uint8_t sr  = 0xFF;
    cs_low();
    (void)HAL_SPI_Transmit(&hspi1, &cmd, 1, SF_TIMEOUT_MS);
    (void)HAL_SPI_Receive(&hspi1, &sr, 1, SF_TIMEOUT_MS);
    cs_high();
    if ((sr & SF_SR_WIP) == 0u)
    {
      return HAL_OK;
    }
    if ((VTICK_MS() - t0) > timeout_ms)
    {
      return HAL_TIMEOUT;
    }
  }
}

#if VECTOR_SPI_DMA
/* ------------------------------------------------------------- DMA-кусок --
 * Можно ли сейчас читать по DMA. Все три условия обязательны:
 *   1) семафор создан (sf_dma_init уже вызван - иначе блокироваться не на чем);
 *   2) DMA не признана неисправной в этой сессии;
 *   3) мы ВНУТРИ потока. tx_thread_identify() возвращает NULL до старта
 *      планировщика, поэтому ext_init()/sf_probe() (они работают до
 *      tx_kernel_enter) гарантированно идут опросом - спать там некому.     */
static int dma_usable(void)
{
  if (!sf_dma_ready || sf_dma_failed)
  {
    return 0;
  }
  return (tx_thread_identify() != (TX_THREAD *)0) ? 1 : 0;
}

/* Приём ОДНОГО куска по DMA: старт -> сон на семафоре до ISR -> проверка.
 * CS уже низкий, команда+адрес уже выданы, SPI в состоянии READY.
 * Перед стартом семафор вычищается: лишний put из прошлой ошибки не должен
 * выглядеть как завершение текущего куска.
 * Возврат: HAL_OK, HAL_TIMEOUT (колбэк не пришёл) или HAL_ERROR/HAL_BUSY.   */
static HAL_StatusTypeDef sf_rx_dma_chunk(uint8_t *dst, uint16_t n)
{
  HAL_StatusTypeDef st;

  while (tx_semaphore_get(&sf_dma_sem, TX_NO_WAIT) == TX_SUCCESS)
  {
    /* сбрасываем "залежавшиеся" взведения */
  }
  sf_dma_err = 0;

  st = HAL_SPI_Receive_DMA(&hspi1, dst, n);
  if (st != HAL_OK)
  {
    return st;                    /* обычно HAL_BUSY: SPI не отпущен прошлым */
  }

  if (tx_semaphore_get(&sf_dma_sem, (ULONG)SF_DMA_TMO) != TX_SUCCESS)
  {
    /* Колбэк не пришёл за SF_DMA_TMO. Гасим SPI и DMA, иначе следующий
       обмен вернёт HAL_BUSY навсегда. */
    (void)HAL_SPI_Abort(&hspi1);
    sf_dbg_dma_tmo++;
    return HAL_TIMEOUT;
  }

  return (sf_dma_err != 0u) ? HAL_ERROR : HAL_OK;
}

/* Создать семафор DMA-приёма. КОНТЕКСТ: tx_application_define() (вызывается
   из ext_init()), до потоков. Повторный вызов безопасен: просто пересоздаёт. */
void sf_dma_init(void)
{
  (void)tx_semaphore_create(&sf_dma_sem, "sf dma", 0);
  sf_dma_failed = 0;
  sf_dma_ready  = 1;
}

/* ============================================ колбэки HAL SPI (КОНТЕКСТ ISR)
 * Цепочка завершения приёма: GPDMA1_Channel10 (данные приняты) -> HAL включает
 * EOT на SPI1 -> SPI1_IRQHandler -> HAL_SPI_RxCpltCallback. Поэтому для работы
 * DMA нужны ОБА прерывания: GPDMA1_Channel10_IRQn и SPI1_IRQn.
 * Здесь нельзя ничего блокирующего - только флаг и tx_semaphore_put().       */

/* Все запрошенные байты приняты: будим поток, спящий в sf_rx_dma_chunk(). */
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance == SPI1)
  {
    (void)tx_semaphore_put(&sf_dma_sem);
  }
}

/* Ошибка обмена (OVR/FRE/MODF/UDR или ошибка канала DMA). Флаг увидит
   sf_rx_dma_chunk(), а поток перечитает блок опросом.                       */
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance == SPI1)
  {
    sf_dma_err = 1;
    sf_dbg_dma_err++;
    (void)tx_semaphore_put(&sf_dma_sem);
  }
}
#else
void sf_dma_init(void) { /* DMA выключена конфигурацией - делать нечего */ }
#endif /* VECTOR_SPI_DMA */

/* ---------------------------------------------------------------- probe ---
 * Проба чипа: RDID (JEDEC) + RDSR. Вызывается из main() до RTOS, поэтому
 * только опрос. Результаты - в sf_jedec/sf_rdsr/sf_probe_rc, их видно в
 * отладчике: sf_jedec = { 0xC2, 0x??, 0x17 } для Macronix 64 Мбит.
 * Возврат: HAL_OK если чип ответил.                                         */
HAL_StatusTypeDef sf_probe(void)
{
  uint8_t cmd = SF_CMD_RDID;
  uint8_t id[3] = { 0, 0, 0 };
  HAL_StatusTypeDef st;

  cs_high();

  cs_low();
  st = HAL_SPI_Transmit(&hspi1, &cmd, 1, SF_TIMEOUT_MS);
  if (st == HAL_OK)
  {
    st = HAL_SPI_Receive(&hspi1, id, 3, SF_TIMEOUT_MS);
  }
  cs_high();
  sf_jedec[0] = id[0];
  sf_jedec[1] = id[1];
  sf_jedec[2] = id[2];

  cmd = SF_CMD_RDSR;
  cs_low();
  if (st == HAL_OK)
  {
    st = HAL_SPI_Transmit(&hspi1, &cmd, 1, SF_TIMEOUT_MS);
  }
  if (st == HAL_OK)
  {
    uint8_t sr = 0xFF;
    st = HAL_SPI_Receive(&hspi1, &sr, 1, SF_TIMEOUT_MS);
    sf_rdsr = sr;
  }
  cs_high();

  sf_probe_rc = (int32_t)st;

  if (st == HAL_OK)
  {
    LOG_I(VLOG_M_FLASH, "flash probe ok: jedec=%x %x %x sr=%x",
          (uint32_t)sf_jedec[0], (uint32_t)sf_jedec[1], (uint32_t)sf_jedec[2],
          (uint32_t)sf_rdsr);
  }
  else
  {
    LOG_E(VLOG_M_FLASH, "flash probe FAIL rc=%d (check SPI1/CS PA4/power)", (int32_t)st);
  }
  return st;
}

/* ---------------------------------------------------------------- read ----
 * Чтение произвольного блока. Единственная "горячая" операция драйвера:
 * подкачка звука тянет до 128 КБ, поэтому именно она умеет DMA.
 *
 * Алгоритм:
 *   1) команда READ + адрес (опрос, CS низкий);
 *   2) приём кусками по SF_DMA_CHUNK: DMA, если она доступна и блок большой,
 *      иначе HAL_SPI_Receive;
 *   3) если DMA подвела - перечитать ВЕСЬ блок опросом с самого начала
 *      (счётчик байт внутри flash после обрыва сдвинут неизвестно как,
 *      поэтому "дочитать хвост" нельзя);
 *   4) CS высокий.
 * Возврат: HAL_OK / HAL_ERROR (адрес вне чипа) / HAL_TIMEOUT.               */
HAL_StatusTypeDef sf_read(uint32_t addr, uint8_t *dst, uint32_t len)
{
  HAL_StatusTypeDef st = HAL_OK;
  uint32_t off;
  uint32_t t0 = VTICK_MS();
#if VECTOR_SPI_DMA
  uint8_t  retried = 0;
  uint8_t  use_dma;
#endif

  if ((addr + len) > SF_TOTAL_SIZE)
  {
    LOG_E(VLOG_M_FLASH, "read out of chip: a=%x l=%u", addr, len);
    return HAL_ERROR;
  }

#if VECTOR_SPI_DMA
  use_dma = (uint8_t)((len >= VECTOR_SPI_DMA_MIN_LEN) ? dma_usable() : 0);
#else
  /* опрос */
#endif

  for (;;)
  {
    off = 0;
    st  = cmd_addr(SF_CMD_READ, addr);      /* CS остаётся НИЗКИМ */

#if VECTOR_SPI_DMA
    while ((st == HAL_OK) && (off < len) && use_dma)
    {
      uint32_t rest  = len - off;
      uint32_t chunk = (rest > SF_DMA_CHUNK) ? SF_DMA_CHUNK : rest;

      st = sf_rx_dma_chunk(dst + off, (uint16_t)chunk);
      if (st != HAL_OK)
      {
        break;                              /* решим ниже: повтор или ошибка */
      }
      sf_dbg_dma_chunks++;
      off += chunk;
    }
#endif

    /* Дочитываем (или читаем всё) блокирующим опросом */
    while ((st == HAL_OK) && (off < len))
    {
      uint32_t rest  = len - off;
      uint32_t chunk = (rest > 0x8000u) ? 0x8000u : rest;  /* предел uint16_t */

      st = HAL_SPI_Receive(&hspi1, dst + off, (uint16_t)chunk, SF_TIMEOUT_MS);
      if (st != HAL_OK)
      {
        break;
      }
      sf_dbg_poll_bytes += chunk;
      off += chunk;
    }

    if ((st == HAL_OK) && (off >= len))
    {
      break;                                /* успех */
    }

#if VECTOR_SPI_DMA
    if ((use_dma != 0u) && (retried == 0u))
    {
      /* DMA не довезла блок. Отключаем её до следующего старта и повторяем
         чтение опросом с нуля - данные важнее скорости. */
      use_dma = 0;
      retried = 1;
      sf_dma_failed = 1;
      sf_dbg_dma_fallback++;
      (void)HAL_SPI_Abort(&hspi1);   /* сначала гасим SPI/DMA, потом CS */
      cs_high();
      LOG_W(VLOG_M_FLASH, "read %x DMA fail (st=%d) -> polling", addr, (int32_t)st);
      st = HAL_OK;                          /* ошибку снимем повтором */
      continue;
    }
#endif
    break;                                  /* настоящая ошибка обмена */
  }

  cs_high();

  if (st == HAL_OK)
  {
    if (len >= VECTOR_SPI_DMA_MIN_LEN)
    {
      LOG_I(VLOG_M_FLASH, "read %u b @%x ok, %u ms", len, addr, VTICK_MS() - t0);
    }
    else
    {
      LOG_D(VLOG_M_FLASH, "read %u b @%x", len, addr);
    }
  }
  else
  {
    LOG_E(VLOG_M_FLASH, "read %u b @%x FAIL st=%d (%u b done)",
          len, addr, (int32_t)st, off);
  }
  return st;
}

/* --------------------------------------------------------------- erase ----
 * Стирание одного сектора 4 КБ. addr ОБЯЗАН быть кратен 4096 (иначе NOR
 * стирает не то, что вы думаете). Последовательность: WREN -> SE -> ждём WIP.
 * Занимает ~40-100 мс (до 400 мс по datasheet), всё это время вызывающий
 * поток занят. Возврат: HAL_OK / HAL_ERROR (неверный адрес) / HAL_TIMEOUT.  */
HAL_StatusTypeDef sf_sector_erase(uint32_t addr)
{
  HAL_StatusTypeDef st;

  if (((addr % SF_SECTOR_SIZE) != 0u) || (addr >= SF_TOTAL_SIZE))
  {
    return HAL_ERROR;
  }
  st = cmd_only(SF_CMD_WREN);
  if (st != HAL_OK)
  {
    return st;
  }
  st = cmd_addr(SF_CMD_SE, addr);
  cs_high();
  if (st != HAL_OK)
  {
    return st;
  }
  {
    uint32_t t0 = VTICK_MS();
    st = wait_busy(SF_ERASE_TMO_MS);
    if (st == HAL_OK)
    {
      LOG_I(VLOG_M_FLASH, "erase @%x ok, %u ms", addr, VTICK_MS() - t0);
    }
    else
    {
      LOG_E(VLOG_M_FLASH, "erase @%x TIMEOUT", addr);
    }
  }
  return st;
}

/* ------------------------------------------------------------- program ----
 * Запись блока с автоматическим переходом через границы страниц 256 Б.
 * Область ДОЛЖНА быть предварительно стёрта (NOR умеет only 1 -> 0).
 * На каждую страницу: WREN -> PP -> ожидание WIP (~0.3-3 мс).
 * Возврат: HAL_OK / HAL_ERROR / HAL_TIMEOUT (первое же failure прерывает).   */
HAL_StatusTypeDef sf_program(uint32_t addr, const uint8_t *src, uint32_t len)
{
  HAL_StatusTypeDef st = HAL_OK;
  uint32_t done = 0;
  uint32_t addr0 = addr;
  uint32_t t0 = VTICK_MS();

  if ((addr + len) > SF_TOTAL_SIZE)
  {
    LOG_E(VLOG_M_FLASH, "write out of chip: a=%x l=%u", addr, len);
    return HAL_ERROR;
  }

  while (done < len)
  {
    uint32_t in_page = addr % SF_PAGE_SIZE;
    uint32_t chunk   = SF_PAGE_SIZE - in_page;
    uint8_t *p       = (uint8_t *)(uintptr_t)(src + done);

    if (chunk > (len - done))
    {
      chunk = len - done;
    }

    st = cmd_only(SF_CMD_WREN);
    if (st != HAL_OK)
    {
      break;
    }
    st = cmd_addr(SF_CMD_PP, addr);
    if (st == HAL_OK)
    {
      st = HAL_SPI_Transmit(&hspi1, p, (uint16_t)chunk, SF_TIMEOUT_MS);
    }
    cs_high();
    if (st != HAL_OK)
    {
      break;
    }
    st = wait_busy(SF_PROG_TMO_MS);
    if (st != HAL_OK)
    {
      break;
    }

    addr += chunk;
    done += chunk;
  }

  if (st == HAL_OK)
  {
    LOG_I(VLOG_M_FLASH, "write %u b @%x ok, %u ms", len, addr0, VTICK_MS() - t0);
  }
  else
  {
    LOG_E(VLOG_M_FLASH, "write %u b @%x FAIL st=%d (%u b done)",
          len, addr0, (int32_t)st, done);
  }
  return st;
}

/* -------------------------------------------------------------- verify ----
 * Побайтное сравнение содержимого flash с образцом (верификация записи).
 * Читает страницами по 256 Б в статический буфер, поэтому памяти не просит,
 * но держит шину на всё время проверки. Возврат: HAL_OK если совпало,
 * HAL_ERROR при первом расхождении или ошибке чтения.                       */
HAL_StatusTypeDef sf_verify(uint32_t addr, const uint8_t *src, uint32_t len)
{
  static uint8_t buf[SF_PAGE_SIZE];
  uint32_t done = 0;

  while (done < len)
  {
    uint32_t chunk = (len - done > sizeof buf) ? sizeof buf : (len - done);
    uint32_t i;
    if (sf_read(addr + done, buf, chunk) != HAL_OK)
    {
      return HAL_ERROR;
    }
    for (i = 0; i < chunk; i++)
    {
      if (buf[i] != src[done + i])
      {
        LOG_E(VLOG_M_FLASH, "verify mismatch @%x: flash=%x sample=%x",
              addr + done + i, (uint32_t)buf[i], (uint32_t)src[done + i]);
        return HAL_ERROR;
      }
    }
    done += chunk;
  }
  return HAL_OK;
}
