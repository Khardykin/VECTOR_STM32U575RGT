/**
  ******************************************************************************
  * @file    Vector_main.c
  * @brief   Потоки прибора (аналог Avis_main.c): приём/парсинг UART и периодика
  *
  *          КАРТА ФАЙЛА
  *            vector_tasks_init()        создать оба потока (из
  *                                       tx_application_define)
  *            receiver_task_function()   ПОТОК "Receiver Task": приём и парсинг
  *                                       ПО КАЖДОМУ UART (COM, LoRa, BLE, LTE, GPS,
  *                                       сенсоры) + Vector_Options_System()
  *            measure_task_function()    ПОТОК "Measure Task": периодика
  *                                       VECTOR_TASKS_MEASURE_PERIOD_MS (1 с)
  *            Vector_Run_Pre_Init()      однократно при старте (аналог
  *                                       Avis_Run_Pre_Init)
  *            Vector_Run_Measure()       измерения прибора + вызов модулей
  *                                       Ble_Run/Lora_Run/Lte_Run/Gps_Run (аналог
  *                                       Avis_Run_Measure)
  *            Vector_RunFlashMemory()    журнал/конфиг во внешней flash
  *            Vector_Options_System()    сервис (аналог Avis_Options_System)
  *            command_message(), Ble_Receive(), Lte_Receive(),
  *            Uart_Channel_Receive(), Ble_Run(), Lora_Run(), Lte_Run(),
 *            Gps_Run()
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
#include "vector_macros.h"

#if (CONFIG_TYPE_LCD_TFT && VECTOR_SCREEN_ROTATION)
#include "TFT.h"          /* TFT_Rotation(): поворот экрана по акселерометру */
#endif

/* --------------------------------------------------------------- потоки ----
 * Приоритеты (в ThreadX МЕНЬШЕ = выше): Audio Player = 10, Receiver = 12,
 * Measure = 13, LVGL = 15. Обмен данными не должен обгонять звук.
 * Стек - в БАЙТАХ (в FreeRTOS xTaskCreate считает слова: 512 слов там =
 * 2048 байт здесь).                                                          */
/* ------------------------------------------------------- данные прибора ---
 * Экземпляры структур, которые объявляет shared_types.h (в Avis они жили в
 * Avis_main.c). Определены ЗДЕСЬ, в файле приложения: если заведёте отдельный
 * модуль конфигурации прибора - перенесите определения туда и удалите эти.
 *
 *   Sns_Cfg_struct      рабочий конфиг и состояние: Status/StateErr, данные
 *                       датчиков (Temperature/Humidity/Pressure), батарея,
 *                       серийный номер, параметры LoRa. Сюда пишет
 *                       bme280_measure(), отсюда читают модули обмена.
 *   Cfg_structdef_read  буфер чтения конфигурации из внешней flash (страница
 *                       CONFIG 0x400000, sfmap.h) - загрузка ещё не реализована.
 *   tempsensor_calib    калибровка температуры BME280 (VECTOR_BME_CALIBRATION 0
 *                       -> пока не используется).
 *   device_turn         прибор включён/выключен (DEVICE_TURNED).
 *
 * ВАЖНО: Sns_Cfg_struct сейчас zero-init, то есть все биты статусов сброшены.
 * Модули обмена проверяют их через TEST_STATUS_COMMON_BIT(), поэтому до
 * загрузки конфига из flash биты включения выставляются в
 * Vector_Run_Pre_Init() по флагам сборки CONFIG_*.                        */
SNS_CFG          Sns_Cfg_struct;
SNS_CFG          Cfg_structdef_read;
TEMPSENSOR_CALIB tempsensor_calib;
DEVICE_TURNED    device_turn = DEVICE_TURNED_ON;

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
  LOG_I(VLOG_M_TASKS, "receiver task: com=%u lora=%u ble=%u lte=%u gps=%u (delay %u ms)",
        (uint32_t)CONFIG_UART, (uint32_t)CONFIG_LORA, (uint32_t)CONFIG_BLE,
        (uint32_t)CONFIG_G4, (uint32_t)CONFIG_GPS,
        (uint32_t)VECTOR_TASKS_RECEIVER_DELAY_MS);

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

    /* --- GPS/GNSS: приём и разбор NMEA -------------------------------------
     * Gps_Receive() реализован в Gps.c (приложен к проекту, закрыт CONFIG_GPS).
     * Координаты оттуда забирает LoRa-трек (Lora_UpdateGPSTrack).            */
#if CONFIG_GPS
    /* if (TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_GPS)) */
    {
      mod_receive_call(TASK_MOD_GPS);
      Gps_Receive();
    }
#endif

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

  /* Датчики на I2C1: BME280 / LIS3DH / MAX17048 (включаются CONFIG_*).
     Результат - в sensors_status.*_ok и в логе.                            */
  sensors_init();

  /* Биты включения модулей обмена. Пока конфиг прибора не читается из
     CONFIG-страницы внешней flash, выставляем их по флагам сборки - иначе
     *_Run() ничего не делают (проверяют TEST_STATUS_COMMON_BIT). Как только
     появится загрузка SNS_CFG (PLAN.md раздел 5, этап 2), эти строки надо
     убрать: биты будут приходить из конфига и по BLE.                      */
#if CONFIG_GPS
  SET_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_GPS);
#endif
#if CONFIG_LORA
  SET_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_LORA);
#endif
#if CONFIG_BLE
  SET_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_BLE);
#endif
#if CONFIG_G4
  SET_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_GSM);
#endif

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

  /* Датчики: BME280 (T/H/P), LIS3DH (ускорение + 6D-ориентация), MAX17048
     (батарея). Значения уходят в Sns_Cfg_struct.Config_common и в
     sensors_status; ошибки шины - в ST_COMMON_BIT_ERR_*.                   */
  sensors_read();

  /* Поворот экрана по акселерометру: прерывание ACCEL_INT (PC11) только
     ставит флаг, ориентацию определяет lis3dh_update_all() внутри
     sensors_read(), а экран крутим здесь - в одном потоке с остальной
     индикацией, чтобы не получить двух владельцев дисплея.                 */
#if (CONFIG_TYPE_LCD_TFT && VECTOR_SCREEN_ROTATION)
  if (sensors_rotation_changed() != 0u)
  {
    TFT_Rotation(sensors_status.screen_rotation);
  }
#endif

  /* Модули обмена вызываются КАЖДЫЙ период, а свой ритм (инициализация,
     передача, сон) каждый модуль держит внутри - в Avis это RTC-таймеры
     TIMER_RTC_BLE_DATA_* / TIMER_RTC_LORA_DATA_*. Порядок как в Avis.        */
  mod_run_call(TASK_MOD_BLE);
  Ble_Run();

  mod_run_call(TASK_MOD_LORA);
  Lora_Run();

  mod_run_call(TASK_MOD_LTE);
  Lte_Run();

  mod_run_call(TASK_MOD_GPS);
  Gps_Run();
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
#if CONFIG_G4
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
	if((device_turn != DEVICE_TURNED_OFF) || (Lora_g_turnoff_system_read() == 1)){
		if((get_state_init_flag_lora_g() == 1)){
			return;
		}
		if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_LORA)){
			if((get_state_init_flag_lora_g() == 0) && (!TEST_TIMER_RUN_RTC(TIMER_RTC_LORA_DATA_INIT)) && (!TEST_TIMER_RTC(TIMER_RTC_LORA_DATA_INIT))){
				START_TIMER_RTC(TIMER_RTC_LORA_DATA_INIT, TIME_RTC_BLE_DATA_INIT);
			}
			if(TEST_TIMER_RTC(TIMER_RTC_LORA_DATA_INIT)){
				RESET_TIMER_RTC(TIMER_RTC_LORA_DATA_INIT);
				Lora_g_Init(&Sns_Cfg_struct);
//				START_TIMER_RTC(TIMER_RTC_LORA_DATA_SLEEP, TIME_RTC_LORA_DATA_SLEEP);
				START_TIMER_RTC(TIMER_RTC_LORA_DATA_SET, Sns_Cfg_struct.Config_common.PeriodTimeLora&0xFF);
				END_TIMER_RTC(TIMER_RTC_LORA_DATA_SET_ALARM);
			}
			if(TEST_TIMER_RTC(TIMER_RTC_LORA_DATA_SET)){
				Lora_g_flag_period_msg_set();
			}

			if(get_state_init_flag_lora_g() == 1){
				uint8_t state_alarm = check_state_alarm_flag_lora_g(&Sns_Cfg_struct);
				if(((state_alarm == 2) && TEST_TIMER_RTC(TIMER_RTC_LORA_DATA_SET_ALARM)) ||
						(state_alarm == 1)){
					RESET_TIMER_RTC(TIMER_RTC_LORA_DATA_SET_ALARM);
					Lora_g_DataSetAlarm(&Sns_Cfg_struct);
					START_TIMER_RTC(TIMER_RTC_LORA_DATA_SET_ALARM, (Sns_Cfg_struct.Config_common.PeriodTimeLora>>8)&0xFF);
				}
				else if(Lora_g_flag_period_msg_read()){
					if(Lora_g_DataSet(&Sns_Cfg_struct)){
						Lora_g_flag_period_msg_clr();
						if(Lora_g_turnoff_system_read() == 1){
							START_TIMER_RTC(TIMER_RTC_LORA_DATA_SET, 10);// Период для отправки пакета выключения0
						}
						else{
							int random = ((rand()%10) - 5);
							START_TIMER_RTC(TIMER_RTC_LORA_DATA_SET, (Sns_Cfg_struct.Config_common.PeriodTimeLora&0xFF) + random);
						}
					}
				}
			}
			if(TEST_TIMER_RTC(TIMER_RTC_LORA_DATA_SLEEP)){
				RESET_TIMER_RTC(TIMER_RTC_LORA_DATA_SLEEP);
//				Lora_SystemSleep();
			}
		}
		else{
			Lora_g_DeInit(0);
		}
		if(get_state_err_lora_g()){
			SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_LORA);
		}
		else{
			CLEAR_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_LORA);
		}
	}
#endif
}

/* LTE/GSM-модем: регистрация в сети, передача данных. */
__attribute__((weak)) void Lte_Run(void)
{
#if CONFIG_G4
  /* TODO: AT-диалог модема (инициализация, регистрация, отправка), контроль
     пинов LTE_EN / LTE_RESET / LTE_STATUS / LTE_LED (main.h), статусы
     ST_COMMON_BIT_TURN_ON_GSM и ST_COMMON_BIT_ERR_GSM.                      */
#endif
}

/* GPS/GNSS: инициализация модуля, периодическое обновление навигационных
   систем, контроль ошибки приёма. Аналог Gps_Run() в Avis_main.c, но вместо
   RTC-таймеров прибора (TIMER_RTC_GPS_DATA_INIT) - тик RTOS: таймеров прибора
   в этом проекте пока нет (PLAN.md раздел 5, этап 2).
   Приём NMEA - Gps_Receive() в потоке receiver_task, НЕ здесь.
   КОНТЕКСТ: поток Measure Task (внутри блокирующий обмен по UART и Delay()). */
__attribute__((weak)) void Gps_Run(void)
{
#if CONFIG_GPS
  static uint32_t nav_last_ms = 0;

  if (!VECTOR_DEVICE_IS_ON())
  {
    Gps_DeInit(0);
    return;
  }

	if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_GPS)){
		if((get_state_init_flag_gps() == 0) && (!TEST_TIMER_RUN_RTC(TIMER_RTC_GPS_DATA_INIT)) && (!TEST_TIMER_RTC(TIMER_RTC_GPS_DATA_INIT))){
			START_TIMER_RTC(TIMER_RTC_GPS_DATA_INIT, TIME_RTC_BLE_DATA_INIT);
		}
		if(TEST_TIMER_RTC(TIMER_RTC_GPS_DATA_INIT)){
			if(Gps_Init(&Sns_Cfg_struct) == 1){
				RESET_TIMER_RTC(TIMER_RTC_GPS_DATA_INIT);
			}
		}
		if(get_state_init_flag_gps() == 1){
			Gps_Init_Nav_Sys(&Sns_Cfg_struct);
		}
	}
	else{
		Gps_DeInit(0);
	}
	if(get_state_err_gps()){
		SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_GPS);
	}
	else{
		CLEAR_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_GPS);
	}
#endif
}

/* ============================================================================
 *                    ДАТЧИКИ I2C1: BME280 / LIS3DH / MAX17048
 * Логика поворота экрана: ACCEL_INT (PC11) -> lis3dh_irq_handler() ставит
 * только флаг -> sensors_read() в этом потоке читает INT1_SRC и определяет
 * ориентацию -> Vector_Run_Measure() зовёт TFT_Rotation(). I2C из ISR не
 * читается никогда.
 * ============================================================================ */
volatile sensors_status_t sensors_status =
{
  .cnt_init_ok  = 0u,
  .last_read_ms = 0u
};

/* Флаг "ориентация изменилась, поворот не применён". Ставится в sensors_read(),
   снимается в sensors_rotation_changed() - то есть событие не теряется, даже
   если экран решено повернуть позже.                                         */
static volatile uint8_t rotation_pending = 0u;

/* ------------------------------------------------------------------ инициализация */
/* Поднять все включённые конфигурацией датчики. Возврата нет: результат по
   каждой микросхеме - в sensors_status.*_ok и в логе.                        */
void sensors_init(void)
{
  sensors_status.cnt_init_ok = 0u;

#if CONFIG_BME
  /* BME280: I2C-адрес 0x76, normal mode, osr_t x8, фильтр 2 (настраивает
     bme280_init_com()). rslt == 0 - микросхема ответила.                     */
  if (bme280_init_com() == 0)
  {
    sensors_status.bme_ok = 1u;
    sensors_status.cnt_init_ok++;
    LOG_I(VLOG_M_SYS, "sensors: BME280 ok (I2C1 0x76)");
  }
  else
  {
    sensors_status.bme_ok = 0u;
    SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_STS4);
    LOG_E(VLOG_M_SYS, "sensors: BME280 FAIL (I2C1 0x76) - check PB8/PB9, addr");
  }
#endif

#if CONFIG_LIS3DH
  /* LIS3DH: WHO_AM_I, HR 12 бит, 100 Гц, +-4g, 6D-ориентация на INT1
     (lis3dh_init() сам настраивает прерывание - см. LIS3DH_ENABLE_6D_INIT).  */
  if (lis3dh_init() == 0)
  {
    sensors_status.lis3dh_ok = 1u;
    sensors_status.cnt_init_ok++;
    LOG_I(VLOG_M_SYS, "sensors: LIS3DH ok (I2C1 0x19), 6D int=%u deg",
          (uint32_t)LIS3DH_6D_THRESHOLD_DEG);
  }
  else
  {
    sensors_status.lis3dh_ok = 0u;
    LOG_E(VLOG_M_SYS, "sensors: LIS3DH FAIL (I2C1 0x19, WHO_AM_I != 0x33)");
  }
#endif

#if CONFIG_MAX17048
  /* MAX17048: топливный счётчик, проверка VERSION (не 0x0000/0xFFFF). */
  if (max17048_init())
  {
    sensors_status.max17048_ok = 1u;
    sensors_status.cnt_init_ok++;
    LOG_I(VLOG_M_SYS, "sensors: MAX17048 ok (I2C1 0x36)");
  }
  else
  {
    sensors_status.max17048_ok = 0u;
    LOG_E(VLOG_M_SYS, "sensors: MAX17048 FAIL (I2C1 0x36)");
  }
#endif

  LOG_I(VLOG_M_SYS, "sensors: init done, %u of 3 modules alive",
        sensors_status.cnt_init_ok);
}

/* -------------------------------------------------------------------- чтение */
/* Прочитать всё, что включено. Звать периодически из Vector_Run_Measure().
   BME280 в normal mode меряет сам, поэтому здесь только забираем результат,
   если MEAS_DONE (иначе счётчик ошибок растёт внутри bme280_measure).        */
void sensors_read(void)
{
  uint32_t t0 = VTICK_MS();

#if CONFIG_BME
  if (sensors_status.bme_ok != 0u)
  {
    /* 0 = данные обновлены, 1 = измерение ещё не готово, <0 = ошибка шины.
       Значения модуль кладёт прямо в Sns_Cfg_struct.Config_common.           */
    if (bme280_measure(&Sns_Cfg_struct) == 0)
    {
      sensors_status.temperature_c = Sns_Cfg_struct.Config_common.Temperature;
      sensors_status.humidity_pct  = Sns_Cfg_struct.Config_common.Humidity;
      sensors_status.pressure_hpa  = Sns_Cfg_struct.Config_common.Pressure;
      sensors_status.cnt_bme_reads++;
      CLEAR_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_STS4);
    }
  }
#endif

#if CONFIG_LIS3DH
  if (sensors_status.lis3dh_ok != 0u)
  {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    /* один вызов = чтение акселерометра (если готов) + разбор события 6D,
       которое прилетело в ACCEL_INT (INT1_SRC читается только здесь)          */
    (void)lis3dh_update_all();

    if (lis3dh_get_cached_accel_g(&x, &y, &z))
    {
      sensors_status.accel_x_g = x;
      sensors_status.accel_y_g = y;
      sensors_status.accel_z_g = z;
      sensors_status.cnt_accel_reads++;
    }

    sensors_status.orientation = (uint8_t)lis3dh_get_orientation();

    /* Смена ориентации -> новый поворот экрана (0 или 2). Сам экран этот
       модуль не трогает: решение принимает Vector_Run_Measure().             */
    if (lis3dh_orientation_changed())
    {
      uint8_t st = lis3dh_get_rotation_state();

      if (st != sensors_status.screen_rotation)
      {
        sensors_status.screen_rotation = st;
        sensors_status.cnt_rotation_events++;
        rotation_pending = 1u;
        LOG_I(VLOG_M_SYS, "sensors: orientation=%u -> rotation=%u",
              (uint32_t)sensors_status.orientation, (uint32_t)st);
      }
    }
  }
#endif

#if CONFIG_MAX17048
  if (sensors_status.max17048_ok != 0u)
  {
    sensors_status.battery_percent_x10 = (uint16_t)(max17048_cellPercent() * 10.0f);
    sensors_status.battery_voltage_mv  = (uint16_t)(max17048_cellVoltage() * 1000.0f);
    sensors_status.cnt_battery_reads++;
  }
#endif

  sensors_status.last_read_ms = VTICK_MS();
  sensors_status.cnt_read_ms  = VTICK_ELAPSED_MS(t0);
}

/* Событие смены ориентации: 1 = надо применить новый поворот
   (sensors_status.screen_rotation), флаг сбрасывается. КОНТЕКСТ: поток.      */
uint8_t sensors_rotation_changed(void)
{
  if (rotation_pending == 0u)
  {
    return 0u;
  }
  rotation_pending = 0u;
  return 1u;
}

/* ============================================================================
 *                                  СЛУЖЕБНОЕ
 * ============================================================================ */

/* Реакция на серию ошибок BME280 (в Avis - Avis_Search_Temp_Start): зовётся из
   bme280_com.c, когда модуль не отвечает или 10 раз подряд не завершает
   измерение. Пока заглушка: фиксируем в логе и в счётчике; наполнение - когда
   появится процедура поиска/переинициализации температурного датчика.
   КОНТЕКСТ: поток Measure Task.                                            */
__attribute__((weak)) void Vector_Search_Temp_Start(uint16_t data)
{
  tasks_status.mod[TASK_MOD_SENSOR].cnt_errors++;
  LOG_W(VLOG_M_SYS, "bme280: error (%u) -> search temp (stub)", (uint32_t)data);
}

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
    case TASK_MOD_GPS:    return "gps";
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
