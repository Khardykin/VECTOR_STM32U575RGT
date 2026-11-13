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
#define VECTOR_AUDIO_DEMO_KEYS      0
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

/* --- Шина внешней flash (ext_mtx): bounded-ожидание вместо TX_WAIT_FOREVER --
 * Flash одна и у звука (чтение, поток Audio Player), и у журнала/конфига
 * (запись, Measure Task). Бесконечное ожидание мьютекса замораживало бы поток
 * измерений вместе с датчиками и *_Run() (бывшие TODO в extstore.c и
 * main_vector.c), поэтому все ожидания ограничены (схема «окно тишины»):
 *   VECTOR_EXT_LOCK_READ_MS  - сколько читателю (ext_read плеера, cfg_load,
 *                              log_read/log_find_by_time журнала) разрешено
 *                              ждать шину, мс. 250 мс
 *                              покрывают худшее стирание сектора (~100 мс);
 *                              плееру ждать допустимо - других дел у потока нет.
 *   VECTOR_EXT_LOCK_WRITE_MS - писателю (save_event-кольцо/cfg_save/ext_write/
 *                              ext_erase_sector): 0 = чистый try-lock, занято ->
 *                              EXT_BUSY (-2) сразу, вызывающий повторит позже.
 *                              Vector_RunFlashMemory() к тому же заходит во
 *                              flash только когда звук молчит (audio_is_busy()
 *                              == 0: доигран или пауза повтора), так что
 *                              коллизии редки. Стирания под звучащий звук не
 *                              начинаются даже изнутри extstore.              */
#ifndef VECTOR_EXT_LOCK_READ_MS
#define VECTOR_EXT_LOCK_READ_MS     250u
#endif
#ifndef VECTOR_EXT_LOCK_WRITE_MS
#define VECTOR_EXT_LOCK_WRITE_MS    0u
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

/* --- РАБОЧЕЕ: потоки прибора (main_vector.c, по образцу Avis_main.c) -------
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

/* --- Отдельные потоки модулей обмена (п.65, решение автора: убрать из
 * measure/receiver - AT-обмен «запрос-ответ» слишком частый для мерного ритма
 * 50 мс; запросов много, они последовательные). Свой поток у каждого радио:
 * BLE (CONFIG_BLE), LoRa (CONFIG_LORA), LTE (CONFIG_G4, «для Lte_Run тоже
 * нужен свой поток» - автор); NB-IoT получит такой же слот (CONFIG_NBIOT).
 * vector_tasks_init() создаёт потоки ТОЛЬКО под включённые CONFIG_*.
 * Приём (Lora_Receive/Lte_Receive) - в receiver_task, в потоках модулей - Run.
 * Приоритеты: audio=10 (AP_PRIORITY) > BLE=11 > receiver=LoRa=LTE=12 > measure=13. */
#ifndef VECTOR_TASKS_BLE_PRIORITY
#define VECTOR_TASKS_BLE_PRIORITY        11u
#endif
#ifndef VECTOR_TASKS_BLE_STACK
#define VECTOR_TASKS_BLE_STACK           2048u
#endif
#ifndef VECTOR_TASKS_BLE_DELAY_MS
#define VECTOR_TASKS_BLE_DELAY_MS        5u
#endif
#ifndef VECTOR_TASKS_LORA_PRIORITY
#define VECTOR_TASKS_LORA_PRIORITY       12u
#endif
#ifndef VECTOR_TASKS_LORA_STACK
#define VECTOR_TASKS_LORA_STACK          2048u
#endif
#ifndef VECTOR_TASKS_LORA_DELAY_MS
#define VECTOR_TASKS_LORA_DELAY_MS       10u
#endif
#ifndef VECTOR_TASKS_LTE_PRIORITY
#define VECTOR_TASKS_LTE_PRIORITY        12u
#endif
#ifndef VECTOR_TASKS_LTE_STACK
#define VECTOR_TASKS_LTE_STACK           2048u
#endif
#ifndef VECTOR_TASKS_LTE_DELAY_MS
#define VECTOR_TASKS_LTE_DELAY_MS        50u
#endif

/* --- Поток дисплея/индикации (LVGL Task) --------------------------------
 *  (вопрос автора: «почему создание потоков для остальных модулей внутри
 *  функций их файлов, а для LVGL прямо в tx_application_define?» - теперь
 *  ЕДИНООБРАЗНО: поток создаёт vector_tasks_init() в main_vector.c, рядом с
 *  receiver/measure/BLE/LoRa/LTE, а не «сбоку» в app_azure_rtos.c).
 *  Макросы - в общем ряду VECTOR_TASKS_*; стек в БАЙТАХ (ThreadX), приоритет
 *  по прежней схеме: audio=10 > BLE=11 > rcv=LoRa=LTE=12 > measure=13 > LVGL=15
 *  (индикация уступает обмену, 15 - средний приоритет, как было в ST-шаблоне).
 *  DELAY - период сна потока: 10 мс, тик движка индикации Alert_IndicationTick(). */
#ifndef VECTOR_TASKS_LVGL_PRIORITY
#define VECTOR_TASKS_LVGL_PRIORITY       15u
#endif
#ifndef VECTOR_TASKS_LVGL_STACK
#define VECTOR_TASKS_LVGL_STACK          4096u
#endif
#ifndef VECTOR_TASKS_LVGL_DELAY_MS
#define VECTOR_TASKS_LVGL_DELAY_MS       10u
#endif

/* --- СТАТУСЫ И ТРЕВОГИ (Alert_vector.c) ----------------------------------
 *  Неподвижность (обездвиженность), отсутствие устройств комплекта
 *  (СИЗ/ГА/браслет), «ОСТОРОЖНО! ГАЗ!», «ВСЕМ НА ВЫХОД», сообщение сервера.
 *  Модель: СТАТУС (признак) и ИНДИКАЦИЯ (звук/свет/вибро) - РАЗНЫЕ сущности:
 *  зелёная кнопка гасит индикацию (квитирование), статус живёт, пока живо
 *  условие (движение не появилось / связь не восстановилась / сервер не снял
 *  бит). Значения по умолчанию - здесь; рабочие (настраиваемые) лежат в
 *  Sns_Cfg_struct.Config_common (Immobility_*, Devices_Expected, Alert_Flags),
 *  макросы ниже - только то, что НЕ меняется с сервера/меню.                 */
#ifndef ALERT_TICK_MS
#define ALERT_TICK_MS                    100u  /* период Alert_Tick, мс      */
#endif

/* Опрос акселерометра на движение: генераторы прерываний LIS3DH заняты
   (INT1 - 6D-ориентация экрана, INT2 - free-fall), поэтому движение ловим
   ОПРОСОМ из Measure Task (владелец I2C1) с периодом 100 мс - резко
   встряхнутый трекер даёт 2..3 g и гарантированно попадает в выборку.      */
#ifndef ALERT_MOTION_POLL_MS
#define ALERT_MOTION_POLL_MS             100u
#endif

/* Порог движения по умолчанию, мг (для Config_common.Immobility_Ths_mg,
   когда в конфиге 0). Акселерометр в HR-режиме, шкала +-4g: шаг 32 мг.     */
#ifndef ALERT_MOTION_THS_MG_DEF
#define ALERT_MOTION_THS_MG_DEF          250u
#endif

/* Значения по умолчанию для настроек неподвижности (DefaultConfig):
   окно без движения -> статус; предаварийный таймер -> инцидент на сервер. */
#ifndef ALERT_IMMOB_WINDOW_S_DEF
#define ALERT_IMMOB_WINDOW_S_DEF         120u  /* 2 мин без движения         */
#endif
#ifndef ALERT_IMMOB_PREALARM_S_DEF
#define ALERT_IMMOB_PREALARM_S_DEF       60u   /* 1 мин на реакцию сотрудника*/
#endif
#ifndef ALERT_IMMOB_ENABLE_DEF
#define ALERT_IMMOB_ENABLE_DEF           1u    /* функция включена с завода  */
#endif

/* Маска ожидаемых устройств комплекта по умолчанию (DefaultConfig).
   Решение автора: состав комплекта берётся АВТОМАТИЧЕСКИ из MAC-таблицы
   BLE-модуля (MACSET/MSDGETALL, DevType: 2 = СИЗ, 3 = браслет, 4 = AVIS) -
   её возвращает Ble_ExpectedDevices() (Ble_vector.c) и Alert_vector.c
   объединяет с этим полем по ИЛИ. Поле - РУЧНОЕ дополнение/переопределение
   (конфигуратор), дефолт 0 = ничего не ждём, пока таблица не прочитана:
   ложных «нет СИЗ/ГА/браслета» не будет. Биты - ALERT_DEV_EXPECT_*.       */
#ifndef ALERT_DEVICES_EXPECTED_DEF
#define ALERT_DEVICES_EXPECTED_DEF       0x0000u
#endif

/* Свежесть данных BLE-телеметрии для статуса «нет X»: берём TTL протокола
   (Ble_telemetry считается свежей BLE_TELEMETRY_TTL_MS = 5 мин).           */
#ifndef ALERT_DEV_STALE_MS
#define ALERT_DEV_STALE_MS               300000u
#endif

/* «Зелёная кнопка» квитирования: номер кнопки прибора (1..3). Автор:
   «Индикация может быть снята вручную нажатием на зеленую кнопку на
   трекере» - какая из трёх кнопок зелёная, УТОЧНИТЬ (пока Button1: она же
   зеркалится в button_sos, LONG не занят в навигации).                     */
#ifndef ALERT_ACK_BUTTON
#define ALERT_ACK_BUTTON                 1u
#endif

/* Маркеры классификации текста downlink-сообщения (LoRa v4 п.7.1: сервер
   шлёт свободный текст CP1251 из каталога PTS). Совпал маркер -> статус с
   голосом; не совпал -> ALERT_MESSAGE (сигнал сообщения).                  */
#define ALERT_MSG_MARK_GAS               "ОСТОРОЖНО! ГАЗ"
#define ALERT_MSG_MARK_EVACUATION        "ВСЕМ НА ВЫХОД"
#define ALERT_MSG_MARK_ID                "ID"       /* «... ID 123» -> параметр */

/* --- ЗВУКИ ТРЕВОГ: КАРТА РОЛЕЙ (автор добавит клипы позже) -----------------
 *  Сейчас голоса НЕТ: ALERT_SND_NONE = 0xFFFF, и Alert_vector.c не выставляет
 *  бит NOTIFY_CH_SOUND - индикация идёт писком BEEP (внутренняя flash, доступен
 *  всегда) + свет/вибро. Как только клипы появятся, голос включится сам:
 *  достаточно заменить ALERT_SND_NONE на индекс клипа (notify_phase_t.sound_idx
 *  - это номер SND_* из audio_ids.h, его играет audio_play_now()).
 *
 *  5 ролей из списка автора и рекомендуемые имена файлов (порядок в команде
 *  сборки задаёт номера SND_*, поэтому имена - заодно и порядок):
 *    роль                     макрос                 файл в tools/
 *    1 «Внимание, загазованность»  ALERT_SND_GAS        alert_gas.wav
 *    2 «Внимание, всем на выход»   ALERT_SND_EVACUATION alert_evacuation.wav
 *    3 сигнал тревоги (период.)    ALERT_SND_ALARM      alert_alarm.wav
 *    4 сигнал сообщения            ALERT_SND_MESSAGE    alert_message.wav
 *    5 сигнал звонка               ALERT_SND_RING       alert_ring.wav
 *
 *  ПОРЯДОК ПОДКЛЮЧЕНИЯ (audio_ids.h СОЗДАЁТСЯ СКРИПТОМ - руками не править):
 *    1. положить 5 wav в tools/ (рядом с имеющимися);
 *    2. python tools\pack_sounds.py tools\*.wav --out tools\sounds.bin
 *       -> пересоберётся tools/sounds.bin (+ .manifest.txt) и ПЕРЕГЕНЕРИРУЕТСЯ
 *       Core/VectorLib/Src/Audio/Inc/audio_ids.h с SND_ALERT_GAS и т.д.;
 *    3. в ЭТОМ файле заменить ALERT_SND_NONE на имена из нового audio_ids.h:
 *         #define ALERT_SND_GAS        SND_ALERT_GAS
 *         #define ALERT_SND_EVACUATION SND_ALERT_EVACUATION
 *         #define ALERT_SND_ALARM      SND_ALERT_ALARM
 *         #define ALERT_SND_MESSAGE    SND_ALERT_MESSAGE
 *         #define ALERT_SND_RING       SND_ALERT_RING
 *    4. прошить новый sounds.bin во внешнюю flash (иначе плеер сыграет клип с
 *       прошлым содержимым образа - номера разъедутся).
 *  Никаких правок Alert_vector.c не требуется: таблица статусов
 *  s_desc[] уже ссылается на эти макросы.                                    */
#define ALERT_SND_NONE                   (0xFFFFu)
#ifndef ALERT_SND_GAS
#define ALERT_SND_GAS                    ALERT_SND_NONE  /* «Внимание, загазованность» */
#endif
#ifndef ALERT_SND_EVACUATION
#define ALERT_SND_EVACUATION             ALERT_SND_NONE  /* «Внимание, всем на выход»  */
#endif
#ifndef ALERT_SND_ALARM
#define ALERT_SND_ALARM                  ALERT_SND_NONE  /* сигнал тревоги (период.)   */
#endif
#ifndef ALERT_SND_MESSAGE
#define ALERT_SND_MESSAGE                ALERT_SND_NONE  /* сигнал сообщения           */
#endif
#ifndef ALERT_SND_RING
#define ALERT_SND_RING                   ALERT_SND_NONE  /* сигнал звонка              */
#endif

/* Период/пауза индикации тревог, мс (как у LedSoundVibroPlay Avis - там
   единицы 100 мс: 10/5 = 1 с период, 0.5 с пауза).                         */
#ifndef ALERT_PERIOD_ALARM_MS
#define ALERT_PERIOD_ALARM_MS            1000u
#endif
#ifndef ALERT_PAUSE_ALARM_MS
#define ALERT_PAUSE_ALARM_MS             500u
#endif
#ifndef ALERT_PERIOD_GAS_MS
#define ALERT_PERIOD_GAS_MS              600u   /* «ОСТОРОЖНО! ГАЗ!» - чаще  */
#endif
#ifndef ALERT_PAUSE_GAS_MS
#define ALERT_PAUSE_GAS_MS               400u
#endif
#ifndef ALERT_PERIOD_EVAC_MS
#define ALERT_PERIOD_EVAC_MS             1000u  /* «ВСЕМ НА ВЫХОД»           */
#endif
#ifndef ALERT_PAUSE_EVAC_MS
#define ALERT_PAUSE_EVAC_MS              300u
#endif
#ifndef ALERT_PERIOD_IMMOB_MS
#define ALERT_PERIOD_IMMOB_MS            500u   /* неподвижность             */
#endif
#ifndef ALERT_PAUSE_IMMOB_MS
#define ALERT_PAUSE_IMMOB_MS             500u
#endif
#ifndef ALERT_PERIOD_DEVICE_MS
#define ALERT_PERIOD_DEVICE_MS           200u   /* нет СИЗ/ГА/браслета       */
#endif
#ifndef ALERT_PAUSE_DEVICE_MS
#define ALERT_PAUSE_DEVICE_MS            300u
#endif
#ifndef ALERT_PERIOD_MSG_MS
#define ALERT_PERIOD_MSG_MS              300u   /* сигнал сообщения          */
#endif
#ifndef ALERT_PAUSE_MSG_MS
#define ALERT_PAUSE_MSG_MS               700u
#endif

/* ОДНОКРАТНЫЕ пачки индикации (колонка burst таблицы s_desc, Alert_vector.c).
   Решение автора по ошибкам прибора и батарее: «ошибки не снимаются, но свет,
   звук и вибро не индицируют - остаются только записи на экране; при повторном
   срабатывании любой из ошибок всё снова звучит и светит». Поэтому:
     burst = 0 - индикация НЕПРЕРЫВНАЯ (газ, эвакуация, неподвижность,
                 «нет СИЗ/ГА/браслета», сообщение): идёт, пока статус активен
                 и не квитирован зелёной кнопкой;
     burst = N - ОДНА пачка из N повторов фазы на факте ПОЯВЛЕНИЯ условия,
                 затем статус молчит и остаётся записью на экране. Новое
                 срабатывание (новый бит ошибки, новый провал напряжения ниже
                 порога) снова даёт пачку.                                    */
#ifndef ALERT_BURST_ERR
#define ALERT_BURST_ERR                  2u    /* ошибка прибора: 2 пика      */
#endif
#ifndef ALERT_BURST_BATLOW
#define ALERT_BURST_BATLOW               2u    /* низкий заряд                */
#endif
#ifndef ALERT_BURST_BATCRIT
#define ALERT_BURST_BATCRIT              3u    /* критический разряд - чаще   */
#endif
#ifndef ALERT_PERIOD_ERR_MS
#define ALERT_PERIOD_ERR_MS              1000u /* ошибка прибора (Avis 10/5)  */
#endif
#ifndef ALERT_PAUSE_ERR_MS
#define ALERT_PAUSE_ERR_MS               500u
#endif
#ifndef ALERT_PERIOD_BATLOW_MS
#define ALERT_PERIOD_BATLOW_MS           200u  /* низкий заряд (Avis 2/3)     */
#endif
#ifndef ALERT_PAUSE_BATLOW_MS
#define ALERT_PAUSE_BATLOW_MS            300u
#endif
#ifndef ALERT_PERIOD_BATCRIT_MS
#define ALERT_PERIOD_BATCRIT_MS          200u  /* критический разряд          */
#endif
#ifndef ALERT_PAUSE_BATCRIT_MS
#define ALERT_PAUSE_BATCRIT_MS           200u
#endif

/* Локальная газовая тревога ПО ПОРОГАМ подключённого газоанализатора Avis
   (автор: «где-то по порогам с Ависа, где-то от сервера»): маска битов
   Connected_device.StateErr, которые считаются загазованностью. По умолчанию
   биты 0..4 = Порог 1/2/3, STEL, TWA (enum ST_GA_ERR, shared_types.h). Бит 5
   «Превышение диапазона» намеренно не входит: это не тревога по газу.      */
#ifndef ALERT_GAS_THR_MASK
#define ALERT_GAS_THR_MASK               0x001Fu
#endif

/* АВТООЧИСТКА статусов с одноразовым источником (колонка hold_s в таблице
   s_desc): пока условие подтверждается, Alert_Set() обновляет метку времени,
   поэтому статус живёт; как только подтверждения нет hold_s секунд - гаснет.
     - газ (ALERT_GAS_HOLD_S): сервер шлёт сообщение, пока не получит MsgAck,
       затем статус держится 5 мин и снимается. Локальные пороги Avis снимают
       его СРАЗУ, как только концентрация вернулась в норму (источник
       отслеживается отдельно, серверную тревогу локальный сброс не гасит);
     - эвакуация (ALERT_EVAC_HOLD_S): снимает сервер (бит EVACUATION в
       ControlLora16 обновляет метку каждую секунду), а локальную тревогу
       режима «мастер» гасим через 10 мин;
     - сообщение (ALERT_MSG_HOLD_S): сигнал сообщения длится минуту, сам текст
       остаётся доступен (Alert_MessageText) для экрана сообщения.           */
#ifndef ALERT_GAS_HOLD_S
#define ALERT_GAS_HOLD_S                 300u
#endif
#ifndef ALERT_EVAC_HOLD_S
#define ALERT_EVAC_HOLD_S                600u
#endif
#ifndef ALERT_MSG_HOLD_S
#define ALERT_MSG_HOLD_S                 60u
#endif

/* Длина текста сообщения сервера в RAM (байт, UTF-8 после конверсии
   CP1251 -> до 2 байт на символ). LORA_MESSAGE_MAX_LEN протокола = 237.    */
#ifndef ALERT_MSG_UTF8_LEN
#define ALERT_MSG_UTF8_LEN               96u
#endif

/* --- БАТАРЕЯ: статусы низкого и критического заряда -----------------------
 * Решение автора: Alert_vector.c сам считает уровни по напряжению MAX17048
 * (sensors_status.battery_charge_volt, мВ) и порогам конфига BAT_LIM_1 (низкий)
 * / BAT_LIM_2 (критический) - машина состояний батареи Avis_Battery_System()
 * в main_vector.c пока weak-заглушка, и биты StateErr никто не взводил. Alert
 * заодно ЗЕРКАЛИТ биты CRITICAL_LOW_BATTERY / CRITICAL_BATTERY в
 * Config_common.StateErr (под Sys_Cfg_Lock/Unlock), чтобы их видели экран
 * (категория 1 очереди ошибок TFT_indicator.c) и сервер.
 * Гистерезис: статус снимается, когда напряжение поднялось выше порога на
 * ALERT_BAT_HYST_MV, иначе на границе порога статус дребезжал бы и каждая
 * «пилка» звучала новой пачкой. Во время зарядки (ST_COMMON_BIT_BAT_CHARGE)
 * статусы не взводятся: низкий заряд на зарядке - штатная ситуация.          */
#ifndef ALERT_BAT_HYST_MV
#define ALERT_BAT_HYST_MV                50u
#endif

/* --- BLE: мастерская сторона модуля MK01 (Ble_vector.c) ---------------
 *  (замечание автора п.2): роли НЕ менялись - как и Avis, мы ХОЗЯИН своего
 * BLE-модуля (порт Ble_mk01.c: AT/ADVBLENAMESET/BLEADDR?/ADVPERIODMS/
 * ADVPOWER/ADVSTART/SLEEP/SYSTEMOFF/DFU/MODINFO?). Отличие одно: вместо пуша
 * своих данных (ADVMSDDATASET) - периодический ЗАПРОС принятых данных
 * ADVMSDDATAGET (Ble_Run по TIMER_RTC_BLE_DATA_SET = 60 с); ответ модуля -
 * hex-структура str_ble_data_set_t (данные отправителя, прибора Avis) ->
 * Connected_device. Тайминги обмена (BLE_WAIT_UART_TIMEOUT/DELAY, повторы) -
 * в Ble_vector.h, все как в Avis.
 * переключатели VECTOR_BLE_FRAME_TMO_MS / VECTOR_BLE_MAX_LINE /
 * VECTOR_BLE_DEV_ID УДАЛЕНЫ: неблокирующая строковая машина больше не нужна
 * (обмен синхронный, команды фиксированные), а dev_id - поле принимаемой
 * структуры, а не наша настройка.                                            */

/* --- Основа меню (Menu_vector.c) ----------------------------------
 * Жесты кнопок декодируются из button_count (счёт в TIM3 1 кГц = мс,
 * ставит EXTI) по образцу Button_1/2/3_Menu из Avis_Menu_2.c:
 *   SHORT  - нажатие дольше антидребезга, выдаётся ОТЛОЖЕННО: на
 *            VECTOR_MENU_DOUBLE_MS после отпускания, если второй клик так и не
 *            пришёл (иначе двойное нажатие давало бы лишнее одиночное действие);
 *   LONG   - удержание >= VECTOR_MENU_PRESS_LONG_MS: срабатывает ПО ТАЙМЕРУ,
 *            пока кнопка ещё зажата (по нему сразу запускается обратный отсчёт -
 *            выключение прибора или SOS), отпускание после него подавляется
 *            (long_fired) и не заряжает SHORT;
 *   DOUBLE - два SHORT: 2-е нажатие началось в окне VECTOR_MENU_DOUBLE_MS
 *            (+ VECTOR_MENU_POLL_MS на квантование 100-мс опроса);
 *   SHORT_LONG - SHORT, затем LONG, начавшийся в том же окне (срабатывает по
 *            таймеру, как LONG);
 *   FAKE   - слишком короткое (дребезг).
 * DOUBLE без отдельного спецдействия на текущем экране обрабатывается как
 * один обычный SHORT (menu_navigate -> menu_as_short): двойное нажатие не
 * должно проглатываться впустую.
 * ВХОД В МЕНЮ: SCREEN_MEASURE -> SCREEN_MENU по Button3 SHORT_LONG.
 * ПИТАНИЕ: включение и выключение прибора - Button3 (LONG): при выключенном
 * приборе Button_Run_Start(), при включённом - Menu_TurnOff() (SCREEN_START и
 * SCREEN_MEASURE, кроме страниц сообщений).
 * SOS - ОТДЕЛЬНЫЙ счётчик button_sos: на одну кнопку PB1 ведутся ДВЕ
 * независимые структуры - button1 (жесты навигации) и button_sos (SOS).
 * В stm32u5xx_it.c button_sos зеркалит Button1 (те же EXTI-флаги, СВОЙ
 * button_count), порог VECTOR_MENU_PRESS_SOS_MS длиннее обычного LONG:
 * удержание Button1 3 с (Avis Button_Sos с BUTTON_PRESS_LONG_SOS). При
 * включении/выключении/отмене отсчёта обе структуры гасятся через
 * Menu_ResetButtons() (Menu_vector.c) - иначе остаются «призрачные» жесты.
 * Обратный отсчёт до подачи SOS - задел автора (экран/роллер ROLLER_SOS и
 * LCD_CountdownToSOS уже есть).
 * Авто-возврат на главный экран: любой жест перезапускает TIMER_RTC_MAIN_RESET
 * (секундный таймер прибора), по его expiry Menu_Run() возвращает SCREEN_MAIN.
 * приоритет жестов ПОСТОЯННЫЙ - Button1 > Button2 > Button3 (автор, п.3);
 * Menu_Run() вызывается из потока LVGL Task по timer.flag_100ms через
 * Vector_Run_Indication() (аналог indication_task_function из Avis_main.c,
 * автор п.5), НЕ из Measure Task; хука Menu_EnterScreen нет (п.1) - экран
 * перерисовывается целиком каждый цикл, weak Menu_Draw() заменит LVGL-порт.
 * Экраны/статусы пока НЕ определены (автор) - enum screen_t расширяется в
 * Menu_vector.h.                                                             */
#ifndef VECTOR_MENU_PRESS_SHORT_MS
#define VECTOR_MENU_PRESS_SHORT_MS  50u
#endif
#ifndef VECTOR_MENU_PRESS_LONG_MS
#define VECTOR_MENU_PRESS_LONG_MS   1500u
#endif
#ifndef VECTOR_MENU_PRESS_SOS_MS
#define VECTOR_MENU_PRESS_SOS_MS    3000u  /* удержание Button1 (button_sos) до SOS */
#endif
#ifndef VECTOR_MENU_DOUBLE_MS
#define VECTOR_MENU_DOUBLE_MS       400u
#endif
/* Период опроса меню: Menu_Run() вызывается из потока LVGL Task по
   timer.flag_100ms, поэтому и начало нажатия, и окно DOUBLE измеряются с
   квантованием до VECTOR_MENU_POLL_MS. Константа компенсирует это в окне
   двойного клика (иначе настоящий двойной с паузой 250-400 мс распадался бы
   на два одиночных). Держать равной периоду опроса.                       */
#ifndef VECTOR_MENU_POLL_MS
#define VECTOR_MENU_POLL_MS         100u
#endif
#ifndef VECTOR_MENU_AUTO_RETURN_S
#define VECTOR_MENU_AUTO_RETURN_S   30u
#endif
#ifndef VECTOR_MENU_ITEMS
#define VECTOR_MENU_ITEMS           4u   /* пунктов в SCREEN_MENU (заглушка: по
                                            числу экранов SquareLine после MAIN) */
#endif

/* --- ЭКРАН ИЗМЕРЕНИЙ (SCREEN_MEASURE, дизайн ui_ScreenMeasurements) --------
 * Страницы и раскладки панелей - таблицы meas_pages[] / meas_layout[] в
 * TFT_indicator.c (LCD_Measure/LCD_MeasureScreen).
 * VECTOR_MEASURE_PAGES       число страниц экрана: ОДНА страница = режимы двух
 *                           панелей (PanelMeasure слева, PanelMeasure1 справа);
 *                           в режимах «координаты / пульс» одна панель
 *                           растягивается на весь контейнер, вторая гасится
 *                           (MEAS_NONE). Страницы 5 и 6 - ОДНА страница
 *                           «Сообщения» с выбором панели (подсветка = куда
 *                           уведёт LONG Button3: 5 = голосовые, 6 = чат).
 *                           Макрос задаёт и размер meas_pages[] (совпадение
 *                           проверяет статический ассерт), и предел скролла в
 *                           menu_navigate() (case SCREEN_MEASURE).
 * VECTOR_LCD_MEASURE_AUTOFIT 1 = значение (LabelMeas) печатается шрифтом
 *                           number85, а если строка шире панели - number36,
 *                           затем number21 (в ui.h объявлены только эти три:
 *                           number85/36/21/14/9). Штатное "143" из дизайна -
 *                           144 px при ширине панели 152 px, поэтому "23.5"
 *                           (167 px) и "101.3" (215 px) без подбора обрезались
 *                           бы по границе панели. 0 = всегда number85.        */
#ifndef VECTOR_MEASURE_PAGES
#define VECTOR_MEASURE_PAGES        8u
#endif
#ifndef VECTOR_LCD_MEASURE_AUTOFIT
#define VECTOR_LCD_MEASURE_AUTOFIT  1
#endif

/* --- РАБОЧЕЕ: экран TFT (ST7789P3 172x320, SPI2 + GPDMA1 Channel8) ---------
 * Драйвер - Core/VectorLib/Src/TFT/LCD_platform.c (переведён с AT32 на HAL,
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
 * Чтение - sensors_read() в main_vector.c: конфигурационные измерения (T/H/P,
 * батарея) -> Sns_Cfg_struct.Config_common, живость чипов -> маска
 * sensors_status.Sensors_ok; runtime-данные (оси Accel_x/y/z, статусы падения,
 * копия Orientation/Screen_rotation) и счётчики диагностики шины I2C1 ->
 * sensors_status (main_vector.h).
 * Включение каждой микросхемы - CONFIG_BME / CONFIG_LIS3DH / CONFIG_MAX17048 в
 * config_device.h: при 0 драйвер не компилируется вовсе (тело файла под #if).
 *
 * VECTOR_SCREEN_ROTATION   1 = при смене 6D-ориентации LIS3DH (прерывание
 *                          ACCEL_INT, PC11) Measure Task вызывает
 *                          TFT_Rotation(sensors_status.Screen_rotation).
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
 * Строка стоит в Vector_Run_Board_Init() (до RTOS, main_vector.c).                              */
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
#define VECTOR_LOG_UART           3
#endif

/* Порог по умолчанию: VLOG_DEBUG(1) VLOG_INFO(2) VLOG_WARN(3) VLOG_ERROR(4) */
#ifndef VECTOR_LOG_LEVEL
#define VECTOR_LOG_LEVEL          2u
#endif

/* Какие модули печатаем по умолчанию: VLOG_M_SYS | _FLASH | _AUDIO */
#ifndef VECTOR_LOG_MASK
#define VECTOR_LOG_MASK           0xFFFFFFFFu
#endif

///* --- [ВРЕМЕННЫЙ ТЕСТ - УДАЛИТЬ ПОСЛЕ ПРОВЕРКИ] звук + журнал одновременно ---
// * 1 = в потоке measure раз в секунду работает SndLogTest_Run1s()
// *     (Core/VectorLib/Src/Test/*/snd_log_test.*):
// *       - громкость 0%, звук SND_ZVONOK_1 зациклен с паузой 1 с
// *         (период между звуками 1 с);
// *       - каждую секунду TYPE_LOG_PARAM_NOP -> RAM-очередь (log_post,
// *         LOG_FORMAT §6); писатель Vector_RunFlashMemory (50 мс) переносит
// *         во flash (program под звуком разрешён, erase в окно тишины);
// *       - раз в 5 с отчёт в терминал (VLOG_M_TASKS): req/q/drop/written/
// *         defers/errors, ext_dbg_*, контрольное чтение последней записи.
// * УДАЛЕНИЕ: этот блок, include в main_vector.h, вызов в main_vector.c
// * (помечены "[ВРЕМЕННЫЙ ТЕСТ]") и оба файла snd_log_test.*.                 */
#ifndef VECTOR_SND_LOG_TEST
#define VECTOR_SND_LOG_TEST       0   /* 1 = тест включён, 0 = выключен.
                                         ВЫКЛЮЧЕН после прогонов 5-6 (журнал v2
                                         + очередь + program под звуком -
                                         ЗАЧЁТ, PLAN п.58-62). Файлы
                                         snd_log_test.* и хуки оставлены ПОД
                                         ЭТИМ DEFINE (решение автора):
                                         включить обратно = 1. Там же ручка
                                         SLT_SET_WH_UNIX - разовая коррекция
                                         часов RTC (PLAN п.62).              */
#endif

#endif /* VECTOR_CONFIG_H */
