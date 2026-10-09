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
  *                                       такту timer.flag_1s (1 с от TIM3)
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

#include "tx_api.h"
#include "vector_log.h"
#include "vector_tick.h"
#include "vector_macros.h"
#include "stm32u5xx_it.h"   /* button1/button2/button3, timer_end_data_exchange */
#include "spiflash.h"       /* sf_probe()/sf_jedec: борд-инит */
#include <stdlib.h>         /* rand(): джиттер периода передачи LoRa */

#if (CONFIG_TYPE_LCD_TFT && VECTOR_SCREEN_ROTATION)
#include "TFT.h"            /* TFT_Rotation(): поворот экрана по акселерометру */
#endif

/* ============================================================================
 *  ТАКТЫ ПРИБОРА И БОРД-ИНИЦИАЛИЗАЦИЯ
 *  Вне #if VECTOR_TASKS_ENABLE: их зовут прерывания (TIM3, RTC wakeup) и
 *  main() - они обязаны существовать всегда.
 *
 *  ТРИ ИСТОЧНИКА ВРЕМЕНИ В ПРОЕКТЕ (каждый для своего):
 *    тик ThreadX (10 мс, vector_tick.h) - VTICK_MS/VTICK_SLEEP_MS: сны потоков,
 *        таймауты модулей (SPI-DMA, экран, watchdog звука);
 *    TIM3 1 кГц  - countdown_time[] (мс) и флаги timer.flag_1ms/10ms/100ms/1s:
 *        логика прибора и таймауты кадров UART;
 *    RTC wakeup 1 с - countdown_time_rtc[] (с): то, что должно жить и во сне
 *        (периоды передачи LoRa/BLE, мото-часы working_hours).
 *  Макросы START/TEST/RESET/END_TIMER[_RTC] - в shared_macros.h.
 * ============================================================================ */
volatile Timer_variables timer = {0};
volatile DOWN_TIMER      countdown_time = {0};
volatile DOWN_TIMER_RTC  countdown_time_rtc = {0};

/* Декремент миллисекундных таймеров. КОНТЕКСТ: прерывание TIM3. */
static void Countdown_Timer(void)
{
	uint8_t i = 0;
	for(i = 0; i < COUNT_TIMERS; i++)
	{
		if(countdown_time.Timers[i])
		{
			countdown_time.Timers[i] --;
			if(!countdown_time.Timers[i])
				END_TIMER(i);
		}
	}
}

/* Декремент секундных таймеров. КОНТЕКСТ: прерывание RTC wakeup. */
static void Countdown_Timer_Rtc(void)
{
	uint8_t i = 0;
	for(i = 0; i < COUNT_TIMERS_RTC; i++)
	{
		if(countdown_time_rtc.Timers[i])
		{
			countdown_time_rtc.Timers[i] --;
			if(!countdown_time_rtc.Timers[i])
				END_TIMER_RTC(i);
		}
	}
}

/* Сбросить все секундные таймеры (например, при пробуждении из сна). */
void Clear_Timer_Rtc(void)
{
	uint8_t i = 0;
	for(i = 0; i < COUNT_TIMERS_RTC; i++)
	{
		if(countdown_time_rtc.Timers[i])
		{
			countdown_time_rtc.Timers[i] = 0;
			END_TIMER_RTC(i);
		}
	}
}

/* Такт 1 мс: таймеры, флаги 1/10/100/1000 мс, счётчики кнопок, таймауты
   кадров UART. Звать из HAL_TIM_PeriodElapsedCallback(TIM3) - одна строка.
   КОНТЕКСТ: прерывание, только счёт и флаги.                                */
void Timer_Tick_1ms(void)
{
	Countdown_Timer();

	timer.flag_1ms = 1;
	timer.count_1ms++;
	if(timer.count_1ms >= 10){
		timer.count_1ms = 0;
		timer.flag_10ms = 1;
		timer.count_10ms++;
		if((timer.count_10ms % 10) == 0){
			timer.flag_100ms = 1;
			timer.count_100ms++;
			if(timer.count_100ms >= 10){
				timer.count_10ms = 0;
				timer.count_100ms = 0;
				timer.flag_1s = 1;
				timer.count_1s++;
			}
		}
	}

	/* длинное нажатие: счётчик растёт, пока кнопка удерживается */
	if(button1.button_flag){
		button1.button_count++;
	}
	if(button2.button_flag){
		button2.button_count++;
	}
	if(button3.button_flag){
		button3.button_count++;
	}

	/* таймауты кадров периферийных UART */
#if CONFIG_UART
	Uart_Command_Receive_Timer_Inc();
#endif
#if CONFIG_GPS
	Uart_Gps_Receive_Timer_Inc();
#endif
#if CONFIG_LORA
	Uart_Lora_Receive_Timer_Inc();
#endif
}

/* Такт 1 с: секундные таймеры, мото-часы, флаг старта, таймер окончания
   режима обмена данными. Звать из HAL_RTCEx_WakeUpTimerEventCallback() - одна
   строка. Источник такта: RTC WakeUp (CK_SPRE, ровно 1 Гц) - куб запускает его
   с _IT и разрешает RTC_IRQn (rtc.c), колбэк живёт в stm32u5xx_it.c.
   КОНТЕКСТ: прерывание.                                                     */
void Timer_Tick_1s(void)
{
	timer.flag_start_work = 1;
	timer.flag_start_work_bat_sys = 1;

	/* Мото-часы: решение автора - единица поля = 1 с (такт RTC wakeup ровно
	   1 с), поэтому += 1. В Avis поле было в единицах 937.5 мкс и значение
	   читалось из RTC (working_hours = RTC_ReadTime()); при подключении
	   сохранения/загрузки конфига (working_hours_offset) единицы не смешивать. */
	Sns_Cfg_struct.Config_common.working_hours += 1;

	Countdown_Timer_Rtc();

#if CONFIG_UART
	if(timer_end_data_exchange){
		timer_end_data_exchange --;
		if(!timer_end_data_exchange){
			flag_end_data_exchange = 1;
		}
	}
#endif
}

/* Таймаут кадра COM-порта. Weak-заглушка: реализация появится вместе с
   command_message() (её счётчик - в buffer_uart_command или подобном).       */
__attribute__((weak)) void Uart_Command_Receive_Timer_Inc(void)
{
}

/* ============================================================================
 *  БОРДОВАЯ ИНИЦИАЛИЗАЦИЯ ДО RTOS  (бывший vector_board.c)
 *  Звать из main() (USER CODE BEGIN 2) ОДНИМ вызовом - до MX_ThreadX_Init().
 *  КОНТЕКСТ: стек MSP, планировщика нет - спать нельзя, паузы только
 *  DelayInt() (цикл ядра), лог работает (vlog_init ещё не вызван - печатает
 *  без мьютекса).
 * ============================================================================ */
void Vector_Run_Board_Init(void)
{
#if VECTOR_AUDIO_SELFTEST
  uint32_t i;
#endif

  /* 0. Такт SRAM4 (16 КБ @0x28000000) по умолчанию ВЫКЛЮЧЕН (RCC_AHB3ENR.31):
        без него обращение к секции .sram4 (buffer_transmit[] из buffer.c) -
        это bus fault. Включаем до первого использования. Для сна потом не
        забыть биты SRAM4PD/SRAM4PDS.                                        */
  __HAL_RCC_SRAM4_CLK_ENABLE();

#if VECTOR_DBG_FREEZE_TICK
  /* Остановка тайм-базы HAL (TIM6), пока ядро стоит на брейкпоинте: иначе
     внутренние таймауты HAL "сгорают" за один останов. На боевой прошивке
     (без отладчика) ни на что не влияет.                                     */
  DBGMCU->APB1FZR1 |= DBGMCU_APB1FZR1_DBG_TIM6_STOP;
#endif

  /* 1. Оконечный усилитель: SD_MODE (PC9) в low = shutdown, звука не будет.
        Правильное место - CubeMX (PC9 -> GPIO output level = High), тогда шаг
        убирается; до тех пор страховка здесь.                               */
  GPIO_WritePin(SD_MODE_GPIO_Port, SD_MODE_Pin, PIN_SET);
  DelayInt(5);

  /* 2. Проба внешней SPI flash: sf_jedec = { C2 28 17 } (MX25R6435F),
        sf_probe_rc = 0. Чистый опрос, RTOS не нужен.                        */
  (void)sf_probe();
  LOG_I(VLOG_M_SYS, "probe rc=%d jedec=%x %x %x", (int32_t)sf_probe_rc,
        (uint32_t)sf_jedec[0], (uint32_t)sf_jedec[1], (uint32_t)sf_jedec[2]);

#if VECTOR_AUDIO_SELFTEST
  /* 3. Прямая проверка звукового тракта (const-PCM -> SAI DMA -> усилитель),
        без очереди/потока/внешней flash. 0 = тракт работает.                */
  for (i = 0; i < (uint32_t)VECTOR_AUDIO_SELFTEST; i++)
  {
    if (audio_selftest() != 0)
    {
      break;
    }
    DelayInt(300);
  }
#endif
}

#if VECTOR_TASKS_ENABLE

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
     * Макросы и Sns_Cfg_struct определены (shared_macros.h/shared_types.h), но
     * бит ST_COMMON_BIT_DATA_EXCHANGE ставить пока некому: модуль COM
     * (command_message) не перенесён - guard раскомментируется вместе с ним.  */
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
     * Gps_Receive() реализован в Gps.c (CONFIG_GPS включён автором). Байты из
     * USART1_IRQHandler идут прямо в Gps_Data_Verification() (свой буфер в
     * Gps.c, мимо колец InputBuffer). Координаты забирает LoRa-трек
     * (Lora_UpdateGPSTrack). Guard из Avis (TURN_ON_GPS) закомментирован: бит
     * временно ставит Vector_Run_Pre_Init() по флагам сборки - до загрузки
     * конфига из CONFIG-страницы внешней flash.                              */
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
 * Один раз делает Vector_Run_Pre_Init(), затем по флагу timer.flag_1s
 * (такт 1 с от TIM3) вызывает Vector_Run_Measure() и
 * Vector_RunFlashMemory().                                                    */
static void measure_task_function(ULONG thread_input)
{
  (void)thread_input;

  /* Однократная инициализация прибора (в Avis - по флагу
     device_turn_to_freertos == DEVICE_TURNED_OFF_PRE_INIT). */
  Vector_Run_Pre_Init();
  tasks_status.cnt_pre_init++;

  tasks_status.measure_running = 1u;
  LOG_I(VLOG_M_TASKS, "measure task: 1 s tick, delay %u ms",
        (uint32_t)VECTOR_TASKS_MEASURE_DELAY_MS);

  while (1)
  {
    tasks_status.cnt_measure_passes++;

    /* Такт 1 с ставит Timer_Tick_1ms() (TIM3); флаг сбрасываем сами - как в
       measure_task_function на других приборах.                            */
    if (timer.flag_1s)
    {
      timer.flag_1s = 0;

      tasks_status.cnt_measure_runs++;
      tasks_status.last_measure_ms = VTICK_MS();

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

  //----------------------------------------------------------------------------
  // Для уменьшения потребления, пин PWR_WAKEUP_PIN4_LOW_1 на всякий случай, так как в слип наврятли мы войдем
	HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN4_LOW_1);
	HAL_FLASHEx_ConfigLowPowerRead(FLASH_LPM_ENABLE);
	HAL_PWREx_EnableUltraLowPowerMode();
	__HAL_RCC_MSIKSTOP_DISABLE();
	__HAL_RCC_HSISTOP_DISABLE();
  //----------------------------------------------------------------------------

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
    TFT_Rotation(Sns_Cfg_struct.Config_common.Screen_rotation);
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

/* LoRa S7678S: инициализация, периодическая передача, аварийный пакет, сон.
   Перенос Lora_Run() из Avis_main.c под драйвер этого проекта (Lora_S7678S.c:
   Lora_Init/Lora_DataSet/Lora_DeInit/Lora_IsTxBusy/get_state_*_lora).
   Ритм - секундные таймеры TIMER_RTC_LORA_DATA_*, период берётся из конфига
   (PeriodTimeLora: младший байт - обычный период, старший - аварийный, х10 с).
   Приём - Lora_Receive() в потоке receiver_task, НЕ здесь.
   КОНТЕКСТ: поток Measure Task.                                            */
__attribute__((weak)) void Lora_Run(void)
{
#if CONFIG_LORA
	if((device_turn != DEVICE_TURNED_OFF) || (Lora_system_msg_read() == 1))
	{
		/* В Avis здесь была проверка READ_PIN_OUT(WRLS_ON) - общего пина питания
		   радио на этой плате нет, поэтому её нет.                             */
		uint16_t time_data_set   = (uint16_t)((Sns_Cfg_struct.Config_common.PeriodTimeLora & 0xFF) * 10);
		uint16_t time_data_set_a = (uint16_t)(((Sns_Cfg_struct.Config_common.PeriodTimeLora >> 8) & 0xFF) * 10);

		if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_LORA)){
			if((get_state_init_flag_lora() == 0) && (!TEST_TIMER_RUN_RTC(TIMER_RTC_LORA_DATA_INIT)) && (!TEST_TIMER_RTC(TIMER_RTC_LORA_DATA_INIT))){
				START_TIMER_RTC(TIMER_RTC_LORA_DATA_INIT, TIME_RTC_LORA_DATA_INIT);
			}
			if(TEST_TIMER_RTC(TIMER_RTC_LORA_DATA_INIT)){
				RESET_TIMER_RTC(TIMER_RTC_LORA_DATA_INIT);
				Lora_Init(&Sns_Cfg_struct);
				START_TIMER_RTC(TIMER_RTC_LORA_DATA_SET, time_data_set);
			}
			/* аварийный статус - передать сразу, не дожидаясь периода */
			if(test_state_alarm_flag_lora(&Sns_Cfg_struct)){
				END_TIMER_RTC(TIMER_RTC_LORA_DATA_SET);
			}
			if(TEST_TIMER_RTC(TIMER_RTC_LORA_DATA_SET) && (!Lora_IsTxBusy())){
				RESET_TIMER_RTC(TIMER_RTC_LORA_DATA_SET);
				Lora_DataSet(&Sns_Cfg_struct);
				START_TIMER_RTC(TIMER_RTC_LORA_DATA_SLEEP, TIME_RTC_LORA_DATA_SLEEP);
				if(Lora_system_msg_read() == 1){
					START_TIMER_RTC(TIMER_RTC_LORA_DATA_SET, 10);   /* пакет выключения - быстрее */
				}
				else if(get_state_alarm_flag_lora() || get_state_repeat_flag_lora()){
					/* джиттер +-5 с, чтобы приборы в сети не сталкивались пакетами */
					int random = ((rand() % 10) - 5);
					START_TIMER_RTC(TIMER_RTC_LORA_DATA_SET, (uint16_t)(time_data_set_a + random));
				}
				else{
					START_TIMER_RTC(TIMER_RTC_LORA_DATA_SET, time_data_set);
				}
			}
			if(TEST_TIMER_RTC(TIMER_RTC_LORA_DATA_SLEEP)){
				RESET_TIMER_RTC(TIMER_RTC_LORA_DATA_SLEEP);
//				Lora_SystemSleep(time_data_set);   /* сон модуля между передачами - включить, когда понадобится */
			}
		}
		else{
			Lora_DeInit(0);
		}

		if(get_state_err_lora()){
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

/* GPS/GNSS: инициализация модуля, обновление навигационных систем, контроль
   ошибки приёма. Перенос Gps_Run() из Avis_main.c (секундные таймеры прибора).
   Приём NMEA - Gps_Receive() в потоке receiver_task, НЕ здесь.
   КОНТЕКСТ: поток Measure Task (внутри блокирующий обмен по UART).          */
__attribute__((weak)) void Gps_Run(void)
{
#if CONFIG_GPS
	if(VECTOR_DEVICE_IS_ON()){
		if(TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_GPS)){
			/* Питание/сброс GNSS: пины GNSS_MODE (PB4) и GNSS_RST (PB5) в кубе
			   есть, но полярность и длительность импульса по схеме не
			   подтверждены - пока не дёргаем. Как подтвердите, добавьте перед
			   Gps_Init(): SET_ON(GNSS_RST); DelayInt(10); SET_OFF(GNSS_RST);  */
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

		/* нет валидных кадров NMEA дольше 20 с - считает сам Gps.c */
		if(get_state_err_gps()){
			SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_GPS);
		}
		else{
			CLEAR_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_GPS);
		}
	}
	else{
		Gps_DeInit(0);
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
volatile sensors_status_t sensors_status = {0};

/* Флаг "ориентация изменилась, поворот не применён": ставится в sensors_read(),
   снимается в sensors_rotation_changed().                                    */
static volatile uint8_t rotation_pending = 0u;

/* Поднять все включённые конфигурацией датчики. Результат - биты
   Config_common.Sensors_ok, счётчик sensors_status.cnt_init_ok и лог.
   КОНТЕКСТ: поток Measure Task (из Vector_Run_Pre_Init).                     */
void sensors_init(void)
{
  Sns_Cfg_struct.Config_common.Sensors_ok = 0u;
  sensors_status.cnt_init_ok = 0u;

#if CONFIG_BME
  /* BME280: адрес 0x76, normal mode, osr_t x8, фильтр 2 (bme280_init_com). */
  if (bme280_init_com() == 0)
  {
    Sns_Cfg_struct.Config_common.Sensors_ok |= SENSORS_OK_BME;
    sensors_status.cnt_init_ok++;
    LOG_I(VLOG_M_SYS, "sensors: BME280 ok (I2C1 0x76)");
  }
  else
  {
    SET_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_STS4);
    LOG_E(VLOG_M_SYS, "sensors: BME280 FAIL (I2C1 0x76) - PB8/PB9, address");
  }
#endif

#if CONFIG_LIS3DH
  /* LIS3DH: WHO_AM_I, HR 12 бит, 100 Гц, +-4g, 6D-ориентация на INT1
     (lis3dh_init сам настраивает прерывание - LIS3DH_ENABLE_6D_INIT).        */
  if (lis3dh_init() == 0)
  {
    Sns_Cfg_struct.Config_common.Sensors_ok |= SENSORS_OK_LIS3DH;
    sensors_status.cnt_init_ok++;
    LOG_I(VLOG_M_SYS, "sensors: LIS3DH ok (I2C1 0x19), 6D th=%u deg",
          (uint32_t)LIS3DH_6D_THRESHOLD_DEG);
  }
  else
  {
    LOG_E(VLOG_M_SYS, "sensors: LIS3DH FAIL (I2C1 0x19, WHO_AM_I != 0x33)");
  }
#endif

#if CONFIG_MAX17048
  if (max17048_init())
  {
    Sns_Cfg_struct.Config_common.Sensors_ok |= SENSORS_OK_MAX17048;
    sensors_status.cnt_init_ok++;
    LOG_I(VLOG_M_SYS, "sensors: MAX17048 ok (I2C1 0x36)");
  }
  else
  {
    LOG_E(VLOG_M_SYS, "sensors: MAX17048 FAIL (I2C1 0x36)");
  }
#endif

  LOG_I(VLOG_M_SYS, "sensors: init done, alive mask=%u (%u of 3)",
        (uint32_t)Sns_Cfg_struct.Config_common.Sensors_ok, sensors_status.cnt_init_ok);
}

/* Прочитать всё, что включено. Значения кладём ТОЛЬКО в
   Sns_Cfg_struct.Config_common - единственное место данных прибора; в
   sensors_status остаются счётчики диагностики шины.
   КОНТЕКСТ: поток Measure Task (раз в секунду).                              */
void sensors_read(void)
{
  uint32_t t0 = VTICK_MS();

#if CONFIG_BME
  if ((Sns_Cfg_struct.Config_common.Sensors_ok & SENSORS_OK_BME) != 0u)
  {
    /* 0 = данные обновлены (модуль сам пишет Temperature/Humidity/Pressure в
       Config_common), 1 = измерение не готово, <0 = ошибка шины.             */
    if (bme280_measure(&Sns_Cfg_struct) == 0)
    {
      sensors_status.cnt_bme_reads++;
      CLEAR_STATUS_COMMON_ERR_BIT(ST_COMMON_BIT_ERR_STS4);
    }
  }
#endif

#if CONFIG_LIS3DH
  if ((Sns_Cfg_struct.Config_common.Sensors_ok & SENSORS_OK_LIS3DH) != 0u)
  {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    /* один вызов = чтение акселерометра (если готов) + разбор события 6D,
       пришедшего в ACCEL_INT (INT1_SRC читается только здесь)                */
    (void)lis3dh_update_all();

    if (lis3dh_get_cached_accel_g(&x, &y, &z))
    {
      sensors_status.cnt_accel_reads++;
    }

    Sns_Cfg_struct.Config_common.Orientation = (uint8_t)lis3dh_get_orientation();

    /* Смена ориентации -> новый поворот экрана (0 или 2). Сам экран модуль не
       трогает: TFT_Rotation() зовёт Vector_Run_Measure().                    */
    if (lis3dh_orientation_changed())
    {
      uint8_t st = lis3dh_get_rotation_state();

      if (st != Sns_Cfg_struct.Config_common.Screen_rotation)
      {
        Sns_Cfg_struct.Config_common.Screen_rotation = st;
        sensors_status.cnt_rotation_events++;
        rotation_pending = 1u;
        LOG_I(VLOG_M_SYS, "sensors: orientation=%u -> rotation=%u",
              (uint32_t)Sns_Cfg_struct.Config_common.Orientation, (uint32_t)st);
      }
    }
  }
#endif

#if CONFIG_MAX17048
  if ((Sns_Cfg_struct.Config_common.Sensors_ok & SENSORS_OK_MAX17048) != 0u)
  {
    Sns_Cfg_struct.Config_common.battery_charge_percent = (uint16_t)max17048_cellPercent();
    Sns_Cfg_struct.Config_common.battery_charge_volt    = (uint16_t)(max17048_cellVoltage());
    sensors_status.cnt_battery_reads++;
  }
#endif

  sensors_status.last_read_ms = VTICK_MS();
  sensors_status.cnt_read_ms  = VTICK_ELAPSED_MS(t0);
}

/* Событие смены ориентации: 1 = надо применить новый поворот
   (Config_common.Screen_rotation), флаг сбрасывается. КОНТЕКСТ: поток.       */
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
