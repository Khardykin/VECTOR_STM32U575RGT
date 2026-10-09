/**
  ******************************************************************************
  * @file    vector_config.h
  * @brief   ЦЕНТРАЛЬНЫЕ переключатели VectorLib: что тестовое, что рабочее
  *
  *          Всё, что помечено ТЕСТ, отключается одним макросом и не попадает
  *          в продакшн-сборку. Рабочие модули (плеер, extstore, мост-парсер)
  *          от этих макросов не зависят.
  ******************************************************************************
  */
#ifndef VECTOR_CONFIG_H
#define VECTOR_CONFIG_H

/* --- ТЕСТ: демо-кнопки (PB1/PB2/PB3) переключают звуки и громкость --------
 * 1 = обработчики EXTI и логика из Core/VectorLib/Test/audio_demo.c активны.
 * 0 = кнопки отдаются вашему рабочему обработчику; плеер управляется
 *     только вызовами audio_play()/audio_play_now()/audio_stop().
 * Ловится ОБА фронта, уровень пина читается в обработчике (антидребезг
 * по вашей схеме), см. audio_demo.c.                                        */
#ifndef VECTOR_AUDIO_DEMO_KEYS
#define VECTOR_AUDIO_DEMO_KEYS      1
#endif

/* --- Сколько раз ПОВТОРЯТЬ сбойное ЧТЕНИЕ внешней flash --------------------
 * Под отладчиком ядро периодически останавливается (Live Watch / Expressions
 * refresh), и SPI-обмен, попавший на такой halt, возвращается по таймауту HAL
 * (st=3, 0 байт). Это НЕ порча flash: данные целы, спотыкается только одно
 * чтение. Без повтора такой глюк обрывал звучание (stream: read fail) или
 * принятие валидного образа на старте (image: header read fail).
 *
 * N = число попыток на одно чтение (1 = без повторов). Между попытками
 * короткий сон RTOS (2 мс), чтобы дать halt'у пройти. Действует в ext_read()
 * (стриминг звука, журнал, конфиг) и в load_image() (таблица образа).
 * Диапазон: 1..16. Разумный default 3.                                     */
#ifndef VECTOR_SF_READ_RETRY
#define VECTOR_SF_READ_RETRY        3
#endif

/* Активный уровень кнопки: 1 = нажатию соответствует лог.1 на пине.
 * ДОЛЖЕН быть согласован с подтяжкой, которую задаёт CubeMX (gpio.c):
 *   GPIO_PULLUP   -> пин в покое = 1, кнопка замыкает на GND -> уровень 0
 *   GPIO_PULLDOWN -> пин в покое = 0, кнопка замыкает на +3V3 -> уровень 1
 * Сейчас в gpio.c у PB1/PB2/PB3 стоит GPIO_PULLUP, поэтому 0. Если поставить
 * наоборот (уровень 1 при pull-up), звук будет стартовать не по нажатию, а по
 * ОТПУСКАНИЮ кнопки: демо-логика ловит оба фронта и событием считает только
 * тот, на котором пин в активном уровне.                                     */
#ifndef VECTOR_KEY_PRESSED_LEVEL
#define VECTOR_KEY_PRESSED_LEVEL    0
#endif

/* --- Подтяжка кнопок PB1/PB2/PB3 (vector_buttons_init в gpio.c) -----------
 * CubeMX генерирует кнопки как GPIO_MODE_IT_RISING + GPIO_NOPULL: плавающий
 * вход и только ОДИН фронт. Демо-логика (audio_demo.c) ловит ОБА фронта и
 * сама решает по уровню пина, было нажатие или отбой, поэтому пины нужно
 * доконфигурировать. Делаем это кодом в USER CODE-секции gpio.c - она
 * переживает регенерацию из CubeMX.
 *   0 = без подтяжки (внешний резистор на плате)
 *   1 = pull-up   (кнопка замыкает пин на GND  -> активный уровень 0)
 *   2 = pull-down (кнопка замыкает пин на +3V3 -> активный уровень 1)
 * Правило: подтяжка должна тянуть пин в НЕактивное состояние, т.е.
 * VECTOR_KEY_PRESSED_LEVEL=1 <-> VECTOR_KEY_PULL=2 (и наоборот).
 *
 * ВНИМАНИЕ: с коммита 7239e3c подтяжку и оба фронта EXTI задаёт САМ CubeMX
 * (gpio.c: GPIO_MODE_IT_RISING_FALLING + GPIO_PULLUP), поэтому макрос больше
 * ни на что не влияет и оставлен только как памятка.                        */
#ifndef VECTOR_KEY_PULL
#define VECTOR_KEY_PULL             2
#endif

/* Антидребезг кнопок, мс. Отсчёт ВЕДЁТСЯ ОТДЕЛЬНО ПО КАЖДОЙ КНОПКЕ, поэтому
   три нажатия подряд разными кнопками не съедают друг друга (раньше интервал
   был общий на все три). 200 мс - это много для органов управления: при
   подтяжке и логике "событие = фронт с активным уровнем пина" достаточно
   30-50 мс.                                                                */
#ifndef VECTOR_KEY_DEBOUNCE_MS
#define VECTOR_KEY_DEBOUNCE_MS      50u
#endif

/* --- РАБОЧЕЕ: SPI1 читает большие блоки по DMA ---------------------------
 * 1 = sf_read() для блоков >= VECTOR_SPI_DMA_MIN_LEN байт уходит в DMA
 *     (HAL_SPI_Receive_DMA + ожидание на семафоре), поток на это время СПИТ,
 *     а не крутится в опросе SPI. Мелкие обмены (команды, статус, журнал)
 *     остаются на блокирующем опросе - там DMA только мешал бы.
 *     Любая ошибка/таймаут DMA -> автоматический откат на опрос внутри того
 *     же удержания CS, поэтому чтение не "ломается", а деградирует.
 * 0 = всё как раньше, чистый опрос (HAL_SPI_Receive).
 * Требует: SPI1_RX = GPDMA1_Channel10, включённые GPDMA1_Channel10_IRQn И
 * SPI1_IRQn (колбэк RxCplt приходит из прерывания EOT самого SPI).          */
#ifndef VECTOR_SPI_DMA
#define VECTOR_SPI_DMA              1
#endif

/* Порог, с которого чтение уходит в DMA (байт). Меньше - опросом. */
#ifndef VECTOR_SPI_DMA_MIN_LEN
#define VECTOR_SPI_DMA_MIN_LEN      1024u
#endif

/* Размер куска одной DMA-транзакции. HAL принимает uint16_t Size, поэтому
   максимум 65535; 32768 = 13 мс при 20 МГц - удобный и ровный кусок.       */
#ifndef VECTOR_SPI_DMA_CHUNK
#define VECTOR_SPI_DMA_CHUNK        32768u
#endif

/* Таймаут ожидания завершения одного DMA-куска, мс. */
#ifndef VECTOR_SPI_DMA_TMO_MS
#define VECTOR_SPI_DMA_TMO_MS       250u
#endif

/* --- ДИАГНОСТИКА: двойное чтение flash при старте потока плеера -----------
 * N>0 = поток плеера один раз прогоняет sf_selftest(): N проходов по 8 КБ,
 *   каждый блок читается ДВАЖДЫ и сравнивается побайтно. Расхождение =
 *   битовые ошибки на линии SPI (наводки/провода/20 МГц на пределе) - одна из
 *   причин "хрипа" при чистом образе. Результат: лог + sf_dbg_selftest_bad
 *   (должно быть 0) + sf_dbg_selftest_runs. Цена: ~N*8 мс один раз при старте.
 * 0 = проверка выключена (боевая прошивка).
 * Для теста "хрипа" рекомендуется 8; после локализации причины - 0.          */
#ifndef VECTOR_SPI_SELFTEST
#define VECTOR_SPI_SELFTEST         0u
#endif

/* --- ОТЛАДКА: поток плеера не спит вечно ---------------------------------
 * >0 = tx_semaphore_get(&ap_wake) ждёт НЕ вечно, а указанное число мс, и по
 *      таймауту поток делает холостой проход (перепроверяет очередь, плюсит
 *      audio_status.cnt_wake_timeouts). Цена - одно пробуждение в секунду, польза -
 *      пропажа события (не прилетел колбэк DMA, не сработала кнопка) больше
 *      НЕ выглядит как вечное зависание, и её видно по счётчикам.
 * 0    = честное TX_WAIT_FOREVER (как было).                                */
#ifndef VECTOR_AUDIO_WAKE_TIMEOUT_MS
#define VECTOR_AUDIO_WAKE_TIMEOUT_MS 1000u
#endif

/* --- ОТЛАДКА: что проиграть сразу после старта плеера ---------------------
 * 0 = ничего: тишина, ждём команд (боевой режим)
 * 1 = звук #0 ПО КРУГУ (включается повтор): проверяет ВЕСЬ тракт - внешняя
 *     flash -> чтение -> громкость -> SAI -> DMA -> усилитель, и заодно
 *     механизм повтора с паузой VECTOR_AUDIO_LOOP_PAUSE_MS
 * 2 = звук #0 образа один раз
 * 3 = аварийный писк из ВНУТРЕННЕЙ flash - проверяет только выходной тракт
 *     (SAI/DMA/усилитель/SD_MODE) и не зависит от внешней flash. Если писка
 *     нет - проблема в SAI/усилителе, а не в памяти.
 * Значения 1 и 2 удобно ставить на время отладки: сразу слышно, жив ли звук,
 * и сразу отрабатывает путь start_now() -> ext_read -> SAI DMA (тот самый,
 * на котором ставят точку останова).                                       */
/* --- ОТЛАДКА: писки при старте, ДО запуска RTOS (audio_selftest) ----------
 * Значение = СКОЛЬКО РАЗ пискнуть; пауза между писками ~300 мс:
 *   0 = не проверять (боевой режим)
 *   1 = один писк  - быстро проверить, жив ли тракт
 *   3 = три писка  - удобнее слушать и считать
 * Писк берётся из const-массива во ВНУТРЕННЕЙ flash и уходит в SAI-DMA
 * напрямую: внешняя flash, образ, очередь и поток плеера в проверке НЕ
 * участвуют. Проверяется только выходной тракт SAI -> DMA -> усилитель ->
 * динамик. Длительность одного писка 240 мс (10584 сэмпла на 44.1 кГц).
 *
 * КАК ЧИТАТЬ РЕЗУЛЬТАТ (цикл в Vector_Run_Board_Init() обрывается на первом же сбое):
 *   слышны ВСЕ заказанные писки, в логе столько же строк
 *   "selftest: done, guard=..." -> цифровая часть работает. Если звука при
 *       этом нет - проблема аналоговая: SD_MODE (PC9), питание усилителя,
 *       обвязка, динамик (чек-лист docs/AUDIO.md, раздел 7.4);
 *   ни одного писка, в логе "selftest: SAI DMA start FAIL hal=N"
 *       (возврат 1) -> HAL_SAI_Transmit_DMA отказала: SAI не инициализирован
 *       или канал занят;
 *   частая "очередь" писков около секунды и тишина, в логе "selftest: DMA
 *   started but NOT finished (GPDMA1_Channel11_IRQn?)" (возврат 2) ->
 *       circular DMA прокручивает писк по кругу, но прерывание завершения
 *       блока не приходит: в NVIC не включён GPDMA1_Channel11_IRQn.
 *   Возврат дублируется в audio_status.cnt_errors / audio_status.last_error.
 *
 * Побочный бонус: пауза между писками отсчитывается циклами ядра
 * (DelayInt(300) в Vector_Run_Board_Init), поэтому при заниженном SYSCLK она
 * растягивается - при реальных 80 МГц вместо 160 было бы ~600 мс. Сам писк
 * при этом всегда 240 мс: его темп задаёт SAI, а не ядро.                    */
#ifndef VECTOR_AUDIO_SELFTEST
#define VECTOR_AUDIO_SELFTEST     0
#endif

#ifndef VECTOR_AUDIO_BOOT_PLAY
#define VECTOR_AUDIO_BOOT_PLAY    0
#endif

/* --- РАБОЧЕЕ: стриминг звука через circular DMA ---------------------------
 * 1 = звук читается из внешней flash КУСКАМИ в две половины небольшого буфера
 *     (ping-pong): DMA в circular mode непрерывно выдаёт буфер в SAI, а поток
 *     дозагружает ту половину, которая только что отыграла
 *     (HAL_SAI_TxHalfCpltCallback -> первая половина, HAL_SAI_TxCpltCallback
 *     -> вторая). Это снимает оба прежних ограничения: длина звука больше не
 *     упирается в 65535 сэмплов / uint16_t Size, а RAM-буфер вместо 131 КБ
 *     занимает 2 x VECTOR_AUDIO_STREAM_CHUNK сэмплов.
 *     ТРЕБУЕТ в CubeMX: SAI1_A -> DMA -> GPDMA1 Channel11 -> Mode = Circular
 *     (тогда hdmatx->Mode = DMA_LINKEDLIST_CIRCULAR и HAL не гасит SAI на TC -
 *     стыки между кусками без щелей и щелчков). Если канал остался в Normal,
 *     плеер сам это определит и пойдёт старым путём одной транзакцией
 *     (с ограничением 1.49 с при 44.1 кГц) и напечатает подсказку в лог.
 * 0 = стриминг выключен, всегда одна DMA-транзакция на звук.
 *
 * VECTOR_AUDIO_STREAM_CHUNK - сэмплов в ОДНОЙ половине буфера. Определяет:
 *   запас времени на дозагрузку = CHUNK / 44100 с (4096 -> 93 мс при том,
 *   что чтение 8 КБ из flash по DMA занимает ~3 мс, то есть запас ~30 раз);
 *   точность окончания звука - хвост добивается тишиной, максимум CHUNK
 *   сэмплов (93 мс); RAM = 2 x CHUNK x 2 байта (4096 -> 16 КБ).             */
#ifndef VECTOR_AUDIO_STREAM
#define VECTOR_AUDIO_STREAM        1
#endif

#ifndef VECTOR_AUDIO_STREAM_CHUNK
#define VECTOR_AUDIO_STREAM_CHUNK  4096u
#endif

/* 1 (по умолчанию) = буфер плеера урезан до двух кусков (16 КБ вместо 131 КБ,
 *     экономия 115 КБ RAM). Безопасно, потому что Mode = Circular для SAI DMA
 *     (GPDMA1 Channel11) задан в .ioc, то есть рабочий режим - стриминг:
 *     в нём длина звука не ограничена буфером.
 * 0 = буфер ПОЛНЫЙ (AUDIO_BUF_SAMPLES, 131 КБ): нужно только если в CubeMX
 *     сброшен Mode = Circular и плеер откатился на путь одной транзакции -
 *     тогда целый звук до 1.49 с (65535 сэмплов @44.1 кГц) читается в буфер
 *     целиком. Длинные звуки в этом режиме не играются (AUDIO_ERR_TOO_LONG). */
#ifndef VECTOR_AUDIO_STREAM_SMALLBUF
#define VECTOR_AUDIO_STREAM_SMALLBUF 1
#endif

/* --- РАБОЧЕЕ: повтор звука (цикл) ----------------------------------------
 * Включается в рантайме: audio_set_loop(1) - повторяется ПОСЛЕДНИЙ
 * запущенный звук с паузой VECTOR_AUDIO_LOOP_PAUSE_MS, пока не будет
 * audio_stop() или audio_set_loop(0). Именно так работает сигнализация:
 * положили ОДИН звук - и он идёт по кругу, ничего держать в коде не нужно.
 * По умолчанию цикл выключен (раньше дефолт задавался макросом
 * VECTOR_AUDIO_LOOP_STATE вместе со слоем состояний, которого больше нет).
 * Одноразовые audio_play()/audio_play_now() встраиваются в цикл: звучат
 * вместо повтора, затем повтор продолжается.                               */

/* Пауза между повторами цикла, мс. Меняется в рантайме: audio_set_loop_pause(). */
#ifndef VECTOR_AUDIO_LOOP_PAUSE_MS
#define VECTOR_AUDIO_LOOP_PAUSE_MS 500u
#endif

/* --- РАБОЧЕЕ: потоки прибора (Vector_main.c, по образцу Avis_main.c) -------
 * Два потока, как на другом приборе:
 *   "Receiver Task" - приём и парсинг ПО КАЖДОМУ UART: command_message() (COM),
 *                     Lora_Receive() (LoRa), Ble_Receive(), Lte_Receive(),
 *                     Uart_Channel_Receive() (сенсоры) + Vector_Options_System();
 *   "Measure Task"  - периодика прибора: Vector_Run_Pre_Init() один раз, затем
 *                     по флагу timer.flag_1s вызов
 *                     Vector_Run_Measure() (внутри Ble_Run/Lora_Run/Lte_Run)
 *                     и Vector_RunFlashMemory().
 *
 * ПРАВИЛО ВЛАДЕЛЬЦА КОЛЬЦА: у каждого UART ровно ОДИН потребитель (SPSC,
 * buffer.h) - поток "Receiver Task". Кольца радио-модулей из других потоков и
 * таймеров не читать.
 *
 * VECTOR_TASKS_ENABLE         1 = потоки создаются (vector_tasks_init() из
 *                                 tx_application_define()); 0 = модуль не
 *                                 собирается, вызовы вырождаются в макросы.
 * VECTOR_TASKS_*_PRIORITY     приоритеты (в ThreadX МЕНЬШЕ = выше):
 *                                 Audio Player = 10, LVGL = 15 - обмен данными
 *                                 не должен обгонять звук.
 * VECTOR_TASKS_*_STACK        стеки в БАЙТАХ (в FreeRTOS xTaskCreate считает
 *                                 слова: 512 слов там = 2048 байт здесь).
 * VECTOR_TASKS_RECEIVER_DELAY_MS  пауза потока приёма между проходами. Тик
 *                                 RTOS = 10 мс, поэтому меньше тика не
 *                                 получится (в Avis было vTaskDelay(2) при
 *                                 тике 1 мс). Байты при этом не теряются: их
 *                                 копит ISR в кольце.
 * VECTOR_TASKS_MEASURE_DELAY_MS   пауза потока измерений между проходами.
 *                                 Период Vector_Run_Measure() макросом НЕ
 *                                 задаётся: это флаг timer.flag_1s (такт 1 с
 *                                 даёт Timer_Tick_1ms() от TIM3).
 *
 * Модули внутри заглушек закрыты своими CONFIG_* из config_device.h
 * (CONFIG_UART, CONFIG_LORA, CONFIG_BLE, CONFIG_G4/CONFIG_G2 для LTE) - как в
 * Avis_main.c: конфигурация выключена -> тело пустое, вызов остаётся.        */
#ifndef VECTOR_TASKS_ENABLE
#define VECTOR_TASKS_ENABLE              1
#endif

#ifndef VECTOR_TASKS_RECEIVER_PRIORITY
#define VECTOR_TASKS_RECEIVER_PRIORITY   12u
#endif

#ifndef VECTOR_TASKS_RECEIVER_STACK
#define VECTOR_TASKS_RECEIVER_STACK      2048u
#endif

#ifndef VECTOR_TASKS_RECEIVER_DELAY_MS
#define VECTOR_TASKS_RECEIVER_DELAY_MS   10u
#endif

#ifndef VECTOR_TASKS_MEASURE_PRIORITY
#define VECTOR_TASKS_MEASURE_PRIORITY    13u
#endif

#ifndef VECTOR_TASKS_MEASURE_STACK
#define VECTOR_TASKS_MEASURE_STACK       4096u
#endif

#ifndef VECTOR_TASKS_MEASURE_DELAY_MS
#define VECTOR_TASKS_MEASURE_DELAY_MS    50u
#endif

/* --- РАБОЧЕЕ: экран TFT (ST7789P3 172x320, SPI2 + GPDMA1 Channel8) ---------
 * Драйвер - Core/VectorLib/Src/TFT/LCD_platform.c (v17 переведён с AT32 на HAL,
 * названия функций сохранены). Периферию задаёт CubeMX: SPI2 8 бит Simplex TX,
 * GPDMA1 Channel8 = SPI2_TX (Normal mode), пины LCD_CS PB14 / LCD_DC PB10 /
 * LCD_RST PB12 / LCD_LED PC6. Модель панели и размеры - CONFIG_MODEL_LCD в
 * config_device.h и TFT_WIDTH/TFT_HEIGHT/TFT_COL_OFFSET в TFT.h.
 *
 * VECTOR_LCD_USE_LVGL       1 = LCD_platform включает lvgl.h и зовёт
 *                           lv_display_flush_ready(disp) по концу DMA-кадра
 *                           (LVGL 9.2 в Drivers/lvgl - см. docs/LVGL.md).
 *                           0 = экран без LVGL: TFT_Init/TFT_Fill/TFT_Test
 *                           работают сами по себе (проверка SPI без графики).
 * VECTOR_LCD_SWAP_RGB565    1 = менять байты пикселя перед выдачей: LVGL рисует
 *                           в little-endian, а ST7789P3 ждёт старший байт
 *                           первым. Цвета правильные без обмена -> ставьте 0.
 * VECTOR_LCD_SPI_TIMEOUT_MS таймаут блокирующей передачи и ожидания свободного
 *                           порта; по истечении порт освобождается принудительно
 *                           (счётчик lcd_status.cnt_wait_timeout).
 * VECTOR_LCD_PWM_PERIOD     период ШИМ подсветки - исторический макрос (Avis
 *                           TMR8 CH3, период 249). Сейчас НЕ используется:
 *                           ШИМ настроен в кубе (TIM8_CH1 PC6 AF3, PSC 159 /
 *                           ARR 39 -> 25 кГц), backlight_set() масштабирует
 *                           скважность от фактического ARR таймера.
 *
 * ЧАСТОТА SPI2: в кубе сейчас Baud Rate Prescaler = 2, то есть 80 МГц при
 * PCLK1 160 МГц. Для ST7789P3 это выше допустимого (запись ~62.5 МГц max) -
 * поставьте /4 (40 МГц) или /8 (20 МГц), иначе возможны битые пиксели и
 * зависания выдачи кадра.                                                    */
#ifndef VECTOR_LCD_USE_LVGL
#define VECTOR_LCD_USE_LVGL            1
#endif

#ifndef VECTOR_LCD_SWAP_RGB565
#define VECTOR_LCD_SWAP_RGB565         1
#endif

#ifndef VECTOR_LCD_SPI_TIMEOUT_MS
#define VECTOR_LCD_SPI_TIMEOUT_MS      200u
#endif

#ifndef VECTOR_LCD_PWM_PERIOD
#define VECTOR_LCD_PWM_PERIOD          249u
#endif

/* --- РАБОЧЕЕ: датчики на I2C1 (BME280, LIS3DH, MAX17048) -------------------
 * Чтение - sensors_read() в Vector_main.c: конфигурационные измерения (T/H/P,
 * батарея) -> Sns_Cfg_struct.Config_common, живость чипов -> маска
 * Config_common.Sensors_ok; runtime-данные (оси Accel_x/y/z, статусы падения,
 * копия Orientation/Screen_rotation) и счётчики диагностики шины I2C1 ->
 * sensors_status (Vector_main.h).
 * Включение каждой микросхемы - CONFIG_BME / CONFIG_LIS3DH / CONFIG_MAX17048 в
 * config_device.h: при 0 драйвер не компилируется вовсе (тело файла под #if).
 *
 * VECTOR_SCREEN_ROTATION   1 = при смене 6D-ориентации LIS3DH (прерывание
 *                          ACCEL_INT, PC11) Measure Task вызывает
 *                          TFT_Rotation(Config_common.Screen_rotation).
 *                          0 = ориентация считается, экран не крутится.
 * VECTOR_BME_CALIBRATION   1 = калибровка температуры BME280 с записью поправки
 *                          во flash (Calib_bme280_Temp из Avis). Сейчас 0:
 *                          механизма калибровки и страницы под неё в проекте
 *                          нет, код вырезан из сборки.     */
#ifndef VECTOR_SCREEN_ROTATION
#define VECTOR_SCREEN_ROTATION         1
#endif

#ifndef VECTOR_BME_CALIBRATION
#define VECTOR_BME_CALIBRATION         0
#endif

/* --- РАБОЧЕЕ: время в проекте берётся ТОЛЬКО от тика RTOS (Azure/ThreadX) --
 * Единственный источник времени приложения - tx_time_get() (SysTick,
 * приоритет 4). Всё через vector_tick.h: VTICK_MS(), VTICK_ELAPSED_MS(),
 * VTICK_SLEEP_MS(), VTICK_MS2TICKS(); метки времени лога - тоже тик RTOS
 * (до планировщика он равен 0, поэтому стартовые строки идут с [0.000]).
 * Разрешение - 10 мс (TX_TIMER_TICKS_PER_SECOND = 100; задаёт CubeMX в
 * tx_initialize_low_level.S). Звук от тика НЕ зависит: темп выдачи сэмплов
 * держит SAI+DMA аппаратно.
 * TIM6 остался только как внутренняя тайм-база HAL (CubeMX -> SYS -> Timebase
 * Source): HAL_GetTick() нужен драйверам HAL для своих таймаутов и больше
 * нигде в прикладном коде не читается.
 * До планировщика тика нет, поэтому там время не используется вовсе:
 * ограничения по числу проходов цикла (wait_busy, guard в audio_selftest).
 */

/* --- ОТЛАДКА: замораживать тайм-базу HAL, пока ядро стоит на брейкпоинте ---
 * 1 = DBGMCU->APB1FZR1 |= DBG_TIM6_STOP. Без этого TIM6 считает и во время
 *     остановки ядра, но прерывание не обслуживается: за halt засчитывается
 *     один тик, и HAL_GetTick() отстаёт от реального времени в десятки раз
 *     (в логе было ~34x: 157 с по часам терминала против 4.65 с по тику).
 *     Отсюда "антидребезг 50 мс длится секунды" и "пауза цикла 40 с".
 * 0 = обычное поведение (таймер крутится и под отладчиком).
 * На боевой прошивке ни на что не влияет - ядро там не останавливается.
 * Строка стоит в Vector_Run_Board_Init() (до RTOS, Vector_main.c).                              */
#ifndef VECTOR_DBG_FREEZE_TICK
#define VECTOR_DBG_FREEZE_TICK    1
#endif

/* --- ОТЛАДКА: лог больших чтений внешней flash (spiflash.c) ----------------
 * 1 = каждое чтение >= VECTOR_SPI_DMA_MIN_LEN печатается в INFO:
 *     "read 8192 b @1acccc ok, 0 ms". При стриминге звука это ~11 строк в
 *     секунду - они забивают терминал и мешают видеть остальной лог.
 * 0 = та же строка уходит в DEBUG-уровень (по умолчанию не печатается).
 * На скорость и надёжность не влияет: это только вывод.                     */
#ifndef VECTOR_SPI_LOG_READS
#define VECTOR_SPI_LOG_READS      0
#endif

/* --- ОТЛАДКА: консольный лог (vector_log.h) -------------------------------
 * Куда печатать - выбирают ДВА независимых переключателя, можно оба сразу.
 *
 * VECTOR_LOG_ITM: 1 = дублировать в ITM/SWO, то есть в консоль ВНУТРИ
 *   CubeIDE (вкладка "SWV"/Serial Wire Viewer, или ITM/Debug printf).
 *   Работает только при подключённом отладчике и только если PB3 свободен:
 *   PB3 = JTDO/TRACESWO, а у вас он сейчас BUTTON3 (EXTI + pull-up) - пока
 *   кнопка на PB3, SWO-вывода физически нет. Освободите PB3 в кубе - и
 *   лог появится в консоли IDE без единого провода.
 *   Если отладчик не подключён, ITM_SendChar() просто ничего не делает.
 *
 * VECTOR_LOG_UART: 0 = не печатать в UART, 1..4 = USART1/USART2/USART3/UART4.
 *   Сейчас 4 - вы видите сообщения в своём терминале на UART4 (PC10, один
 *   провод, полудуплекс). UART4 = лог + терминал COM (USART_COM/USART_DEBUG,
 *   кольцо TYPE_USART); USART1 занят GPS, USART2 - LTE, USART3 - BLE:
 *   значения 1/2/3 конфликтуют с портами модулей.
 *
 * VECTOR_LOG_ENABLE: 0 = ВСЁ выключено на этапе компиляции: макросы LOG_*
 *   становятся ((void)0), vector_log.c пустой, в прошивке не остаётся ни
 *   кода, ни строк. Это главный выключатель "для серии".                    */
#ifndef VECTOR_LOG_ENABLE
#define VECTOR_LOG_ENABLE         1
#endif

#ifndef VECTOR_LOG_ITM
#define VECTOR_LOG_ITM            1
#endif

#ifndef VECTOR_LOG_UART
#define VECTOR_LOG_UART           4
#endif

/* Порог по умолчанию: VLOG_DEBUG(1) VLOG_INFO(2) VLOG_WARN(3) VLOG_ERROR(4) */
#ifndef VECTOR_LOG_LEVEL
#define VECTOR_LOG_LEVEL          2u
#endif

/* Какие модули печатаем по умолчанию: VLOG_M_SYS | _FLASH | _AUDIO */
#ifndef VECTOR_LOG_MASK
#define VECTOR_LOG_MASK           0xFFFFFFFFu
#endif

#endif /* VECTOR_CONFIG_H */
