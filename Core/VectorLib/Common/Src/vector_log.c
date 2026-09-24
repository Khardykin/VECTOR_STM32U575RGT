/**
  ******************************************************************************
  * @file    vector_log.c
  * @brief   Консольный лог: ITM/SWO (консоль CubeIDE) и/или UART
  *
  *          КУДА ПЕЧАТАЕТ (vector_config.h):
  *            VECTOR_LOG_ITM  1 -> ITM_SendChar(), видно в консоли CubeIDE
  *                               (вкладка SWV). Нужен свободный PB3 (TRACESWO).
  *            VECTOR_LOG_UART 4 -> UART4 (PC10), видно в вашем терминале.
  *                               1/2/3 = USART1/2/3, 0 = не печатать в UART.
  *            VECTOR_LOG_ENABLE 0 -> ничего не компилируется вовсе.
  *
  *          Устроен нарочно просто и без зависимостей:
  *            - своя печать %s %d %u %x %X %c %% (без vsnprintf из libc);
  *            - одна статическая строка VLOG_LINE (160 Б) + мьютекс, чтобы
  *              два потока не перемешали вывод;
  *            - передача блокирующая HAL_UART_Transmit с таймаутом: лог не
  *              должен вешать систему, если USART1 не инициализирован;
  *            - из ISR вызов отбрасывается (проверка __get_IPSR()), потому что
  *              блокирующая передача в прерывании недопустима.
  ******************************************************************************
  */
#include "vector_log.h"

#if VECTOR_LOG_ENABLE

#include "main.h"
#include "usart.h"
#include "tx_api.h"
#include <stdarg.h>

#define VLOG_LINE       160u    /* длина одной строки вместе с префиксом      */
#define VLOG_TX_TMO_MS  60u     /* таймаут выдачи строки в UART               */

#if (VECTOR_LOG_UART == 1)
#define VLOG_UART       huart1
#define VLOG_UART_NAME  "USART1"
#elif (VECTOR_LOG_UART == 2)
#define VLOG_UART       huart2
#define VLOG_UART_NAME  "USART2"
#elif (VECTOR_LOG_UART == 3)
#define VLOG_UART       huart3
#define VLOG_UART_NAME  "USART3"
#elif (VECTOR_LOG_UART == 4)
#define VLOG_UART       huart4
#define VLOG_UART_NAME  "UART4"
#else
#define VLOG_UART_NAME  "none"
#endif

/* Лог и мост на одном UART4: предупреждение на этапе компиляции, чтобы потом
   не разбирать "почему в терминале каша". См. vector_config.h.              */
#if (VECTOR_LOG_UART == 4) && VECTOR_UART_BRIDGE_TEST
#warning "vector_log: log and bridge share UART4 - disable one of them (VECTOR_LOG_UART 0 or VECTOR_UART_BRIDGE_TEST 0)"
#endif

static TX_MUTEX        vlog_mtx;
static volatile uint8_t vlog_mtx_ok = 0;   /* мьютекс создан (vlog_init)      */
static char            vlog_line[VLOG_LINE];
static uint8_t         vlog_level = (uint8_t)VECTOR_LOG_LEVEL;
static uint32_t        vlog_mask  = (uint32_t)VECTOR_LOG_MASK;

volatile uint32_t vlog_dbg_lines       = 0;
volatile uint32_t vlog_dbg_isr_skipped = 0;

/* ------------------------------------------------------------ блокировка --
 * Мьютекс нужен, потому что лог пишут несколько потоков (плеер, мост,
 * хранилище). До vlog_init() и до планировщика блокировка не делается -
 * в этот момент исполнитель один. Из ISR сюда не попадают (проверка IPSR).  */
static void vlog_lock(void)
{
  if (vlog_mtx_ok && (tx_thread_identify() != (TX_THREAD *)0))
  {
    (void)tx_mutex_get(&vlog_mtx, TX_WAIT_FOREVER);
  }
}

static void vlog_unlock(void)
{
  if (vlog_mtx_ok && (tx_thread_identify() != (TX_THREAD *)0))
  {
    (void)tx_mutex_put(&vlog_mtx);
  }
}

/* ---------------------------------------------------------------- печать --
 * Один символ в буфер с границей. Возврат: новая длина. */
static uint32_t putc_(char *b, uint32_t n, uint32_t cap, char c)
{
  if (n + 1u < cap)
  {
    b[n++] = c;
  }
  return n;
}

/* Число в буфер по основанию 8/10/16. Возврат: новая длина. */
static uint32_t putnum(char *b, uint32_t n, uint32_t cap, uint32_t v, uint8_t base, uint8_t upper)
{
  char tmp[11];
  uint8_t i = 0;

  if (v == 0u)
  {
    tmp[i++] = '0';
  }
  while (v > 0u)
  {
    uint32_t d = v % base;
    tmp[i++] = (char)((d < 10u) ? ('0' + d)
                                : ((upper != 0u) ? ('A' + d - 10u) : ('a' + d - 10u)));
    v /= base;
  }
  while (i > 0u)
  {
    n = putc_(b, n, cap, tmp[--i]);
  }
  return n;
}

/* Форматирование: поддерживаются %s %d %u %x %X %c %%. Всё остальное
   печатается как есть. Числа 32-битные, %d принимает int32_t.              */
static uint32_t vformat(char *b, uint32_t cap, const char *fmt, va_list ap)
{
  uint32_t n = 0;

  while (*fmt != '\0')
  {
    if (*fmt != '%')
    {
      n = putc_(b, n, cap, *fmt++);
      continue;
    }
    fmt++;
    switch (*fmt)
    {
      case 's':
      {
        const char *s = va_arg(ap, const char *);
        if (s == (const char *)0) { s = "(null)"; }
        while (*s != '\0') { n = putc_(b, n, cap, *s++); }
        break;
      }
      case 'd':
      {
        int32_t v = (int32_t)va_arg(ap, int);
        if (v < 0) { n = putc_(b, n, cap, '-'); v = -v; }
        n = putnum(b, n, cap, (uint32_t)v, 10u, 0u);
        break;
      }
      case 'u':
        n = putnum(b, n, cap, va_arg(ap, uint32_t), 10u, 0u);
        break;
      case 'x':
        n = putnum(b, n, cap, va_arg(ap, uint32_t), 16u, 0u);
        break;
      case 'X':
        n = putnum(b, n, cap, va_arg(ap, uint32_t), 16u, 1u);
        break;
      case 'c':
        n = putc_(b, n, cap, (char)va_arg(ap, int));
        break;
      case '%':
        n = putc_(b, n, cap, '%');
        break;
      case '\0':
        fmt--;                 /* строка закончилась на '%' */
        break;
      default:
        n = putc_(b, n, cap, '%');
        n = putc_(b, n, cap, *fmt);
        break;
    }
    if (*fmt != '\0') { fmt++; }
  }
  return n;
}

/* Буква модуля по маске - чтобы в логе было видно, кто говорит. */
static char mask_char(uint32_t mask)
{
  if (mask & VLOG_M_FLASH)  { return 'F'; }
  if (mask & VLOG_M_AUDIO)  { return 'A'; }
  if (mask & VLOG_M_BRIDGE) { return 'B'; }
  return 'S';
}

/* =============================================================== публичное */
/* Печать одной строки лога. Контекст: поток или инициализация до планировщика.
   Из ISR вызов отбрасывается (счётчик vlog_dbg_isr_skipped).                */
void vlog(uint32_t mask, uint8_t level, const char *fmt, ...)
{
  va_list ap;
  uint32_t n = 0;
  uint32_t ms;

  if ((level < vlog_level) || ((mask & vlog_mask) == 0u) || (fmt == (const char *)0))
  {
    return;
  }
  if (__get_IPSR() != 0u)          /* мы в прерывании - блокироваться нельзя */
  {
    vlog_dbg_isr_skipped++;
    return;
  }

  vlog_lock();

  /* префикс: [секунды.мс] уровень/модуль: */
  ms = HAL_GetTick();
  n = putc_(vlog_line, n, VLOG_LINE, '[');
  n = putnum(vlog_line, n, VLOG_LINE, ms / 1000u, 10u, 0u);
  n = putc_(vlog_line, n, VLOG_LINE, '.');
  {
    uint32_t frac = ms % 1000u;
    n = putc_(vlog_line, n, VLOG_LINE, (char)('0' + (frac / 100u)));
    n = putc_(vlog_line, n, VLOG_LINE, (char)('0' + ((frac / 10u) % 10u)));
    n = putc_(vlog_line, n, VLOG_LINE, (char)('0' + (frac % 10u)));
  }
  n = putc_(vlog_line, n, VLOG_LINE, ']');
  n = putc_(vlog_line, n, VLOG_LINE, ' ');
  n = putc_(vlog_line, n, VLOG_LINE, (char)((level <= 9u) ? ('0' + level) : '?'));
  n = putc_(vlog_line, n, VLOG_LINE, '/');
  n = putc_(vlog_line, n, VLOG_LINE, mask_char(mask));
  n = putc_(vlog_line, n, VLOG_LINE, ':');
  n = putc_(vlog_line, n, VLOG_LINE, ' ');

  va_start(ap, fmt);
  n += vformat(vlog_line + n, VLOG_LINE - n, fmt, ap);
  va_end(ap);

  n = putc_(vlog_line, n, VLOG_LINE, '\r');
  n = putc_(vlog_line, n, VLOG_LINE, '\n');
  vlog_line[n] = '\0';

  /* --- выдача: ITM (консоль CubeIDE) и/или UART (терминал) --- */
#if VECTOR_LOG_ITM
  {
    uint32_t i;
    for (i = 0; i < n; i++)
    {
      /* ITM_SendChar сам проверяет, включён ли трейс: без отладчика это
         несколько тактов вхолостую, без блокировки. */
      (void)ITM_SendChar((uint32_t)vlog_line[i]);
    }
  }
#endif
#if (VECTOR_LOG_UART != 0)
  (void)HAL_UART_Transmit(&VLOG_UART, (uint8_t *)vlog_line, (uint16_t)n, VLOG_TX_TMO_MS);
#endif
  vlog_dbg_lines++;

  vlog_unlock();
}

/* Создать мьютекс лога и напечатать шапку. КОНТЕКСТ: tx_application_define()
   (или раньше - до планировщика мьютекс просто не используется).            */
void vlog_init(void)
{
  if (!vlog_mtx_ok)
  {
    (void)tx_mutex_create(&vlog_mtx, "vlog", TX_NO_INHERIT);
    vlog_mtx_ok = 1;
  }
  LOG_I(VLOG_M_SYS, "--- vector log on: itm=%u uart=" VLOG_UART_NAME " ---",
        (uint32_t)VECTOR_LOG_ITM);
  LOG_I(VLOG_M_SYS, "level=%u mask=%x", (uint32_t)vlog_level, vlog_mask);
}

/* Захватить шину вывода, ЕСЛИ лог идёт в тот же UART, что и ваш (иначе строка
   лога и ваш пакет перемешаются побайтно). Если лог не на этом UART или
   выключен - пустая операция. КОНТЕКСТ: только поток.                       */
void vlog_bus_lock(void *uart_instance)
{
#if (VECTOR_LOG_UART != 0)
  if ((void *)VLOG_UART.Instance == uart_instance)
  {
    vlog_lock();
    return;
  }
#else
  (void)uart_instance;
#endif
}

void vlog_bus_unlock(void *uart_instance)
{
#if (VECTOR_LOG_UART != 0)
  if ((void *)VLOG_UART.Instance == uart_instance)
  {
    vlog_unlock();
    return;
  }
#else
  (void)uart_instance;
#endif
}

void     vlog_set_level(uint8_t level) { vlog_level = level; }
uint8_t  vlog_get_level(void)          { return vlog_level; }
void     vlog_set_mask(uint32_t mask)  { vlog_mask = mask; }
uint32_t vlog_get_mask(void)           { return vlog_mask; }

#endif /* VECTOR_LOG_ENABLE */
