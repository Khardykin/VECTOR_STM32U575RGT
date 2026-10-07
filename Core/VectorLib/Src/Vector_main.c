/**
  ******************************************************************************
  * @file    Vector_main.c
  * @brief   Потоки прибора (аналог Avis_main.c): приём/парсинг UART и периодика
  *
  *          КАРТА ФАЙЛА
  *            vector_tasks_init()        создать оба потока (из
  *                                       tx_application_define)
  *            receiver_task_function()   ПОТОК "Receiver Task": приём и парсинг
  *                                       ПО КАЖДОМУ UART (COM, LoRa, BLE, LTE,
  *                                       сенсоры) + Vector_Options_System()
  *            measure_task_function()    ПОТОК "Measure Task": периодика
  *                                       VECTOR_TASKS_MEASURE_PERIOD_MS (1 с)
  *            Vector_Run_Pre_Init()      однократно при старте (аналог
  *                                       Avis_Run_Pre_Init)
  *            Vector_Run_Measure()       измерения прибора + вызов модулей
  *                                       Ble_Run/Lora_Run/Lte_Run (аналог
  *                                       Avis_Run_Measure)
  *            Vector_RunFlashMemory()    журнал/конфиг во внешней flash
  *            Vector_Options_System()    сервис (аналог Avis_Options_System)
  *            command_message(), Ble_Receive(), Lte_Receive(),
  *            Uart_Channel_Receive(), Ble_Run(), Lora_Run(), Lte_Run()
  *                                       СЛАБЫЕ заглушки модулей: ваша
  *                                       реализация в файле модуля заменит их
  *                                       при линковке сама
  *
  *          ЧТО НАПОЛНЯТЬ: всё, что помечено TODO. Каркас потоков, счётчики
  *          (tasks_status) и порядок вызовов уже рабочие.
  *
  *          ПРАВИЛО ВЛАДЕЛЬЦА КОЛЬЦА (buffer.h, SPSC): у каждого UART ровно
  *          ОДИН потребитель. Кольца радио-модулей читает ТОЛЬКО поток
  *          "Receiver Task" (напрямую или через Lora_Receive()) - из других
  *          потоков/таймеров в InputBuffer[] не лазить.
  *
  *          КОНТЕКСТ: оба потока - обычные потоки ThreadX, здесь можно
  *          блокироваться (мьютексы, сон, лог). Из ISR сюда не заходить.
  ******************************************************************************
  */
#include "Vector_main.h"      /* агрегатор: config_device.h, vector_config.h,
                                   buffer.h, Lora_S7678S.h (при CONFIG_LORA)  */
#include "vector_tasks.h"

#if VECTOR_TASKS_ENABLE

#include "tx_api.h"
#include "vector_log.h"
#include "vector_tick.h"

/* --------------------------------------------------------------- потоки ----
 * Приоритеты (в ThreadX МЕНЬШЕ = выше): Audio Player = 10, Receiver = 12,
 * Measure = 13, LVGL = 15. Обмен данными не должен обгонять звук.
 * Стек - в БАЙТАХ (в FreeRTOS xTaskCreate считает слова: 512 слов там =
 * 2048 байт здесь).                                                          */
static TX_THREAD receiver_task_handler;
static TX_THREAD measure_task_handler;
static uint8_t   receiver_task_stack[VECTOR_TASKS_RECEIVER_STACK] __attribute__((aligned(8)));
static uint8_t   measure_task_stack[VECTOR_TASKS_MEASURE_STACK]   __attribute__((aligned(8)));

static void receiver_task_function(ULONG thread_input);
static void measure_task_function(ULONG thread_input);

/* ------------------------------------------------------------- состояние ---
 * ВСЁ состояние потоков и счётчики модулей - в ОДНОЙ структуре tasks_status
 * (в отладчике достаточно одной строки `tasks_status`). Начальные значения
 * задаются здесь, дальше поля правят потоки и заглушки модулей.              */
volatile tasks_status_t tasks_status =
{
  .receiver_running   = 0u,
  .measure_running    = 0u,
  .cnt_pre_init       = 0u
};

/* Отметить вызов приёма/обмена модуля: счётчик + метка времени (тик RTOS).
   КОНТЕКСТ: потоки прибора.                                                   */
static void mod_receive_call(task_module_t mod)
{
  tasks_status.mod[mod].cnt_receive_calls++;
  tasks_status.mod[mod].last_receive_ms = VTICK_MS();
}

static void mod_run_call(task_module_t mod)
{
  tasks_status.mod[mod].cnt_run_calls++;
  tasks_status.mod[mod].last_run_ms = VTICK_MS();
}

/* ============================================================================
 * ПОТОК ПРИЁМА: здесь принимается и парсится ВСЁ, что приходит по UART'ам.
 * Порядок вызовов - как в receiver_task_function() на другом приборе:
 * сначала COM (режим обмена данными), затем радио-модули, затем сенсорный
 * UART, в конце сервисные действия.
 *
 * Ритм: VTICK_SLEEP_MS(VECTOR_TASKS_RECEIVER_DELAY_MS). В Avis там
 * vTaskDelay(2) при тике 1 мс; здесь тик RTOS = 10 мс (TX_TIMER_TICKS_PER_SECOND
 * = 100), поэтому меньше одного тика спать не получится - это и есть минимальная
 * пауза. Приём байтов из колец при этом не теряется: их копит ISR.            */
static void receiver_task_function(ULONG thread_input)
{
  (void)thread_input;

  tasks_status.receiver_running = 1u;
  LOG_I(VLOG_M_TASKS, "receiver task: com=%u lora=%u ble=%u lte=%u (delay %u ms)",
        (uint32_t)CONFIG_UART, (uint32_t)CONFIG_LORA, (uint32_t)CONFIG_BLE,
        (uint32_t)(CONFIG_G4 || CONFIG_G2), (uint32_t)VECTOR_TASKS_RECEIVER_DELAY_MS);

  while (1)
  {
    tasks_status.cnt_receiver_passes++;

    /* --- COM/терминал: команды обмена данными ------------------------------
     * В Avis вызов закрыт статусом режима обмена:
     *   if (TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_DATA_EXCHANGE)) command_message();
     * В этом приборе Sns_Cfg_struct и макросы статусов пока не определены
     * (shared_types.h не включён в сборку) - как появятся, раскомментируйте.  */
#if CONFIG_UART
    /* if (TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_DATA_EXCHANGE)) */
    {
      mod_receive_call(TASK_MOD_COM);
      command_message();
    }
#endif

    /* --- LoRa S7678S: приём и разбор AT-ответов ----------------------------
     * Lora_Receive() уже реализован в Lora_S7678S.c и сам читает
     * InputBuffer[TYPE_LORA] - поэтому вызываем его, а не свой парсер.
     * ВАЖНО: это ЕДИНСТВЕННОЕ место, где читается кольцо LoRa (правило SPSC). */
#if CONFIG_LORA
    /* if (TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_LORA)) */
    {
      mod_receive_call(TASK_MOD_LORA);
      Lora_Receive();
    }
#endif

    /* --- BLE ---------------------------------------------------------------- */
    mod_receive_call(TASK_MOD_BLE);
    Ble_Receive();

    /* --- LTE/GSM ------------------------------------------------------------ */
    mod_receive_call(TASK_MOD_LTE);
    Lte_Receive();

    /* --- сенсорный UART (каналы измерения) ---------------------------------- */
    mod_receive_call(TASK_MOD_SENSOR);
    Uart_Channel_Receive();

    /* --- сервис: soft reset, стирание flash, конец режима обмена ------------- */
    Vector_Options_System();

    VTICK_SLEEP_MS(VECTOR_TASKS_RECEIVER_DELAY_MS);
  }
}

/* ============================================================================
 * ПОТОК ИЗМЕРЕНИЙ/ПЕРИОДИКИ: аналог measure_task_function() в Avis_main.c.
 * Один раз делает Vector_Run_Pre_Init(), затем каждые
 * VECTOR_TASKS_MEASURE_PERIOD_MS (1 с) вызывает Vector_Run_Measure() и
 * Vector_RunFlashMemory().                                                    */
static void measure_task_function(ULONG thread_input)
{
  uint32_t next_measure_ms;

  (void)thread_input;

  /* Однократная инициализация прибора (в Avis - по флагу
     device_turn_to_freertos == DEVICE_TURNED_OFF_PRE_INIT). */
  Vector_Run_Pre_Init();
  tasks_status.cnt_pre_init++;

  tasks_status.measure_running = 1u;
  next_measure_ms              = VTICK_MS();
  LOG_I(VLOG_M_TASKS, "measure task: period %u ms, delay %u ms",
        (uint32_t)VECTOR_TASKS_MEASURE_PERIOD_MS, (uint32_t)VECTOR_TASKS_MEASURE_DELAY_MS);

  while (1)
  {
    tasks_status.cnt_measure_passes++;

    /* В Avis здесь timer.flag_1s (флаги ставит таймер прибора). Тут время -
       тик RTOS: сравниваем, сколько прошло с предыдущего запуска. Когда
       перенесёте таймеры прибора (TIMER_RTC_*), флаг можно оставить как есть. */
    if (VTICK_ELAPSED_MS(next_measure_ms) >= (uint32_t)VECTOR_TASKS_MEASURE_PERIOD_MS)
    {
      next_measure_ms = VTICK_MS();

      tasks_status.cnt_measure_runs++;
      tasks_status.last_measure_ms = next_measure_ms;

      Vector_Run_Measure();
      Vector_RunFlashMemory();
    }

    VTICK_SLEEP_MS(VECTOR_TASKS_MEASURE_DELAY_MS);
  }
}

/* ============================================================================
 *                               НАПОЛНЕНИЕ ПРИБОРА
 * ============================================================================ */

/* Однократная инициализация при старте потока измерений.
   КОНТЕКСТ: поток measure_task (планировщик уже запущен - можно спать и
   брать мьютексы).                                                           */
void Vector_Run_Pre_Init(void)
{
  LOG_I(VLOG_M_TASKS, "pre init");

  /* TODO: по образцу Avis_Run_Pre_Init():
     - прочитать SNS_CFG из CONFIG-страницы внешней flash (extstore: ext_read,
       контроль CRC_CONFIG) - PLAN.md раздел 3;
     - включить питание/сброс модулей: LTE_EN, LTE_RESET, BLE_RESET, GNSS_RST;
     - инициализировать модули: Lora_Init(&Sns_Cfg_struct), Ble_Init(serial);
     - взвести статусы ST_COMMON (TURN_ON_BLE/LORA/GPS/GSM) из конфига.        */
}

/* Периодика прибора (1 с): измерения, затем модули обмена.
   Аналог Avis_Run_Measure(). КОНТЕКСТ: поток measure_task.                   */
void Vector_Run_Measure(void)
{
  /* TODO: измерения прибора - сенсоры/каналы, пределы и BumpTest, батарея,
     температура/влажность/давление (в Avis: PosChannel_board_init,
     BatChargeCount, Run_Measure, Avis_Run_Temp, lis3dh_update_all,
     Setting_system/LimitProcessing/BumpTest по каналам).
     Звук/индикация по статусам - только через API плеера: audio_play_now(idx),
     audio_set_loop(1), с учётом ST_COMMON_BIT_BLOCK_SOUND (PLAN.md раздел 4).*/

  /* Модули обмена вызываются КАЖДЫЙ период, а свой ритм (инициализация,
     передача, сон) каждый модуль держит внутри - в Avis это RTC-таймеры
     TIMER_RTC_BLE_DATA_* / TIMER_RTC_LORA_DATA_*. Порядок как в Avis.        */
  mod_run_call(TASK_MOD_BLE);
  Ble_Run();

  mod_run_call(TASK_MOD_LORA);
  Lora_Run();

  mod_run_call(TASK_MOD_LTE);
  Lte_Run();
}

/* Журнал и конфигурация во внешней flash. Аналог Avis_RunFlashMemory().
   КОНТЕКСТ: поток measure_task. Шина flash защищена мьютексом extstore
   (ext_mtx) - брать его, а не запрещать прерывания.                          */
void Vector_RunFlashMemory(void)
{
  tasks_status.cnt_flash_runs++;

#if CONFIG_SAVE_PARAM_LOG
  /* TODO: сохранить SNS_CFG при изменении (ext_write в страницу CONFIG
     0x400000) и дописать кольцевой журнал (область LOG 0x401000, sfmap.h).
     В Avis здесь xSemaphoreTake(xSemaphore_flash, ...) - у нас эквивалент
     уже внутри ext_read/ext_write.                                          */
#endif
}

/* Сервисные действия: вызываются из потока приёма каждый проход.
   Аналог Avis_Options_System(). КОНТЕКСТ: поток receiver_task.               */
void Vector_Options_System(void)
{
  /* TODO: по образцу Avis_Options_System():
     - мягкая перезагрузка прошивки (SoftResetProgram) с записью события
       TYPE_FW_UPDATE в журнал и NVIC_SystemReset();
     - стирание внутренней flash по команде (FlashErase);
     - окончание режима обмена данными: очистить ST_COMMON_BIT_DATA_EXCHANGE
       и ST_COMMON_BIT_BLOCK_SOUND;
     - уход в сон при CONFIG_SLEEP (PLAN.md раздел 5, этап 5).                */
}

/* ============================================================================
 *                          МОДУЛИ: ПРИЁМ (поток receiver_task)
 * Слабые (__attribute__((weak))) заглушки. Когда перенесёте реализацию в свой
 * файл модуля (Ble.c, Lte.c, Uart_Channel.c), сильное определение заменит
 * заглушку при линковке - править Vector_main.c не придётся.
 * Внутри каждой заглулки тело закрыто #if CONFIG_* - как в Avis_main.c:
 * модуль выключен конфигурацией, вызов остаётся, тело пустое.
 * ============================================================================ */

/* COM/терминал (UART4): разбор команд обмена данными. */
__attribute__((weak)) void command_message(void)
{
#if CONFIG_UART
  /* TODO: приём из InputBuffer[TYPE_USART] и разбор команд терминала.
     ВНИМАНИЕ: UART4 сейчас занят логом (VECTOR_LOG_UART 4), а эхо в
     UART4_IRQHandler пишет в TDR мимо vlog_bus_lock() - см. PLAN.md
     раздел 7, п.11. Либо перенесите лог на другой порт, либо разведите
     режимы (лог / обмен).                                                   */
#endif
}

/* BLE-модуль (USART3): приём из кольца и парсинг кадров. */
__attribute__((weak)) void Ble_Receive(void)
{
#if CONFIG_BLE
  uint8_t b;

  /* TODO: if (TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_BLE)) { ... } */
  while (check_buffer(&InputBuffer[TYPE_BLE]) != 0u)
  {
    if (receive_buffer(&InputBuffer[TYPE_BLE], &b) == 0u)
    {
      break;                       /* кольцо опустело между проверкой и чтением */
    }
    tasks_status.mod[TASK_MOD_BLE].cnt_rx_bytes++;
    tasks_status.mod[TASK_MOD_BLE].last_receive_ms = VTICK_MS();

    /* TODO: парсинг - байт в машину состояний/кадр, затем статусы прибора и
       команды (Ble_Parsing). Конец кадра: IDLE-линия (UART_IT_IDLE) или пауза
       между байтами - PLAN.md раздел 5, этап 1.                             */
  }
#endif
}

/* LTE/GSM-модем: приём из кольца и парсинг AT-ответов. */
__attribute__((weak)) void Lte_Receive(void)
{
#if (CONFIG_G4 || CONFIG_G2)
  uint8_t b;

  /* TODO: if (TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_GSM)) { ... } */
  while (check_buffer(&InputBuffer[TYPE_LTE]) != 0u)
  {
    if (receive_buffer(&InputBuffer[TYPE_LTE], &b) == 0u)
    {
      break;
    }
    tasks_status.mod[TASK_MOD_LTE].cnt_rx_bytes++;
    tasks_status.mod[TASK_MOD_LTE].last_receive_ms = VTICK_MS();

    /* TODO: парсинг ответов модема (строки AT, URC) - своя машина состояний. */
  }
#endif
}

/* Сенсорный UART: приём данных каналов измерения.
   В Avis - Uart_Channel_Receive(&sensor_uart[0]) по каналу; здесь подпись
   упрощена до void(void), поменяйте на свою, когда перенесёте каналы.        */
__attribute__((weak)) void Uart_Channel_Receive(void)
{
  /* TODO: приём из InputBuffer[TYPE_SENSOR] и разбор кадра датчика.
     Порт сенсоров в кубе ещё не назначен (USART1 PB6/PB7 свободен, но
     USART1_IRQn в NVIC не включён) - см. PLAN.md раздел 7, п.24.            */
}

/* ============================================================================
 *                       МОДУЛИ: ОБМЕН (поток measure_task)
 * ============================================================================ */

/* BLE: инициализация, передача данных, сон. Аналог Ble_Run() в Avis_main.c. */
__attribute__((weak)) void Ble_Run(void)
{
#if CONFIG_BLE
  /* TODO: по образцу Avis_main.c:
     if (TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_BLE)) {
       Ble_Init(serial) / Ble_MsdDataSet(&Sns_Cfg_struct) / Ble_MsdDataSleep()
       по таймерам TIMER_RTC_BLE_DATA_INIT / _SET / _SLEEP
     } else { Ble_DeInit(0); }
     ошибка модуля -> SET/CLEAR_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_BLE). */
#endif
}

/* LoRa: инициализация, передача пакетов, сон. Аналог Lora_Run() в Avis_main.c.
   Драйвер уже есть (Lora_S7678S.c): Lora_Init/Lora_DataSet/Lora_DeInit/
   Lora_IsTxBusy/Lora_SystemSleep/get_state_*_lora.                          */
__attribute__((weak)) void Lora_Run(void)
{
#if CONFIG_LORA
  /* TODO: по образцу Avis_main.c:
     if (TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_LORA)) {
       Lora_Init(&Sns_Cfg_struct) -> Lora_DataSet(&Sns_Cfg_struct) с периодом
       Sns_Cfg_struct.Config_common.PeriodTimeLora, аварийный пакет - раньше,
       проверка !Lora_IsTxBusy() перед передачей
     } else { Lora_DeInit(0); }
     ошибка модуля -> SET/CLEAR_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_LORA).
     Приём - Lora_Receive() в потоке receiver_task, НЕ здесь.                */
#endif
}

/* LTE/GSM-модем: регистрация в сети, передача данных. */
__attribute__((weak)) void Lte_Run(void)
{
#if (CONFIG_G4 || CONFIG_G2)
  /* TODO: AT-диалог модема (инициализация, регистрация, отправка), контроль
     пинов LTE_EN / LTE_RESET / LTE_STATUS / LTE_LED (main.h), статусы
     ST_COMMON_BIT_TURN_ON_GSM и ST_COMMON_BIT_ERR_GSM.                      */
#endif
}

/* ============================================================================
 *                                  СЛУЖЕБНОЕ
 * ============================================================================ */

/* Короткое имя модуля для лога (только ASCII - требование vector_log.h).
   КОНТЕКСТ: любой. Возвращает статическую строку, не освобождать.            */
const char *task_module_name(task_module_t mod)
{
  switch (mod)
  {
    case TASK_MOD_COM:    return "com";
    case TASK_MOD_LORA:   return "lora";
    case TASK_MOD_BLE:    return "ble";
    case TASK_MOD_LTE:    return "lte";
    case TASK_MOD_SENSOR: return "sensor";
    default:              return "?";
  }
}

/* Создать потоки прибора. КОНТЕКСТ: tx_application_define() (стек MSP,
   планировщик ещё не запущен) - как audio_init(): TX_AUTO_START означает, что
   потоки реально побегут после tx_kernel_enter().                            */
void vector_tasks_init(void)
{
  if (tx_thread_create(&receiver_task_handler, "Receiver Task", receiver_task_function, 0,
                       receiver_task_stack, (ULONG)VECTOR_TASKS_RECEIVER_STACK,
                       (UINT)VECTOR_TASKS_RECEIVER_PRIORITY,
                       (UINT)VECTOR_TASKS_RECEIVER_PRIORITY,
                       TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    LOG_E(VLOG_M_TASKS, "receiver task create FAIL (stack=%u prio=%u)",
          (uint32_t)VECTOR_TASKS_RECEIVER_STACK, (uint32_t)VECTOR_TASKS_RECEIVER_PRIORITY);
  }

  if (tx_thread_create(&measure_task_handler, "Measure Task", measure_task_function, 0,
                       measure_task_stack, (ULONG)VECTOR_TASKS_MEASURE_STACK,
                       (UINT)VECTOR_TASKS_MEASURE_PRIORITY,
                       (UINT)VECTOR_TASKS_MEASURE_PRIORITY,
                       TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    LOG_E(VLOG_M_TASKS, "measure task create FAIL (stack=%u prio=%u)",
          (uint32_t)VECTOR_TASKS_MEASURE_STACK, (uint32_t)VECTOR_TASKS_MEASURE_PRIORITY);
  }

  LOG_I(VLOG_M_TASKS, "tasks init: receiver(prio %u stack %u) measure(prio %u stack %u)",
        (uint32_t)VECTOR_TASKS_RECEIVER_PRIORITY, (uint32_t)VECTOR_TASKS_RECEIVER_STACK,
        (uint32_t)VECTOR_TASKS_MEASURE_PRIORITY, (uint32_t)VECTOR_TASKS_MEASURE_STACK);
}

#endif /* VECTOR_TASKS_ENABLE */
