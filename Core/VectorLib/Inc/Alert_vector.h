/**
  ******************************************************************************
  * @file    Alert_vector.h
  * @brief   ЕДИНЫЙ МОДУЛЬ ИНДИКАЦИИ ПРИБОРА: реестр статусов/тревог +
  *          неблокирующий движок «свет / звук / вибро» по фазам.
  *
  *          ОБЪЕДИНЕНИЕ (указание автора: «Notify_vector и Alert_vector -
  *          первый файл избыточен, на нём включение и выключение всего»):
  *          секвенсор оповещений Notify_vector перенесён сюда целиком, файлы
  *          Notify_vector.c/.h удалены. Один модуль = ОДНО место, где
  *          решается, что сейчас звучит/светится/вибрирует и почему:
  *            ВЕРХНИЙ УРОВЕНЬ - СТАТУСЫ (реестр): признак + его индикация;
  *            НИЖНИЙ УРОВЕНЬ - ДВИЖОК: фазы, каналы, тайминги, приоритеты.
  *          API прежнего Notify_vector сохранён по смыслу, имена - Alert_*:
  *            Notify_Tick        -> Alert_IndicationTick (10 мс, LVGL Task)
  *            Notify_Start       -> Alert_Start
  *            Notify_StartEx     -> Alert_StartEx
  *            Notify_Alarm       -> Alert_Signal      (id ALERT_SIG_*)
  *            Notify_AlarmEx     -> Alert_SignalEx
  *            Notify_Stop/Busy   -> Alert_Stop/Alert_Busy
  *            NotifySeq_PowerOn  -> AlertSeq_PowerOn
  *            NOTIFY_CH_*        -> ALERT_CH_*,  NOTIFY_PRIO_* -> ALERT_PRIO_*,
  *            notify_phase_t     -> alert_phase_t, notify_seq_t -> alert_seq_t,
  *            NOTIFY_REPEAT_FOREVER -> ALERT_REPEAT_FOREVER,
  *            NOTIFY_WAIT_END    -> ALERT_WAIT_END.
  *
  *  ============================ ЧАСТЬ 1. СТАТУСЫ ============================
  *          Реестр: неподвижность, отсутствие устройств комплекта
  *          (СИЗ/ГА/браслет), «ОСТОРОЖНО! ГАЗ!», «ВСЕМ НА ВЫХОД», сообщение
  *          сервера, ошибки прибора (ST_COMMON_ERR) и разряд батареи.
  *
  *          ЗАЧЕМ ОТДЕЛЬНЫЙ СЛОЙ (требования автора):
  *            1) «нужен статус неподвижности на акселерометре» - состояние
  *               считается здесь (окно без движения -> статус -> предаварийный
  *               таймер -> инцидент на сервер);
  *            2) «индикация может быть снята нажатием кнопки, но именно
  *               ИНДИКАЦИЯ, а не сам статус; где-то кнопки будут
  *               подтверждением/квитированием» - у каждого статуса ДВА флага:
  *               status (признак, живёт пока живо условие) и indication
  *               (звуковая/световая индикация; кнопка гасит ТОЛЬКО её);
  *            3) «нет СИЗ, ГА, браслета ... индикация НЕ может быть снята
  *               вручную, прекратится когда связь восстановится или устройство
  *               удалено из настроек» - у таких статусов ack_button = 0.
  *               Состав комплекта (решение автора) берётся АВТОМАТИЧЕСКИ из
  *               MAC-таблицы BLE-модуля: Ble_ExpectedDevices() (Ble_vector.c,
  *               DevType 2/3/4) объединяется по ИЛИ с Config_common.
  *               Devices_Expected (ручная маска конфигуратора);
  *            4) «ОСТОРОЖНО! ГАЗ!» и «ВСЕМ НА ВЫХОД» - звук + ГОЛОС + свет,
  *               индикация снимается зелёной кнопкой (квитирование). У газовой
  *               тревоги ДВА источника (автор: «где-то по порогам с Ависа,
  *               где-то от сервера»): пороги своего газоанализатора Avis
  *               (Connected_device.StateErr & ALERT_GAS_THR_MASK) и команда
  *               сервера (текст Message) - учитываются раздельно (s_gas_src),
  *               на экране виден ID трекера-источника. Эвакуацию снимает
  *               сервер (бит EVACUATION), локальную тревогу «мастера» - hold_s;
  *            5) звуки автор добавит позже - индексы клипов вынесены в макросы
  *               ALERT_SND_* (vector_config.h), сейчас ALERT_SND_NONE;
  *            6) ОШИБКИ ПРИБОРА (ST_COMMON_ERR) и БАТАРЕЯ (решение автора:
  *               «ошибки не снимаются, но свет/звук/вибро не индицируют -
  *               остаются только записи на экране; при повторном срабатывании
  *               любой из ошибок всё снова звучит и светит»): у этих статусов
  *               burst > 0 - индикация ОДНОКРАТНАЯ пачка на факте появления
  *               ошибки, дальше статус живёт молча (виден на экране), а новое
  *               срабатывание (новый бит в StateErr / новый провал напряжения)
  *               снова даёт пачку. Кнопкой не снимаются (ack_button = 0).
  *
  *          СВЯЗЬ С ДРУГИМИ СЛОЯМИ:
  *            движение/ускорение  -> lis3dh_motion_poll()/lis3dh_motion_event()
  *                                   (lis3dh.c, опрос 10 Гц из Measure Task);
  *            звук/свет/вибро     -> движок ЧАСТИ 2 этого же файла;
  *            экран               -> TFT_indicator: категория 3 очереди
  *                                   «ошибок» (Alert_Top/Alert_Name/Alert_Param),
  *                                   имена ошибок прибора - Alert_SysErrName();
  *            сервер (LoRa v4)    -> Lora_SetMotionState() (Motion: posture +
  *                                   MOTION_BIT_INACTIVE + ActivityLevel) и
  *                                   Lora_AddContainerEvent(CONTAINER_EVENT_*)
  *                                   - инцидент «Отсутствие активности» (0x10);
  *            состав комплекта    -> Ble_ExpectedDevices() (MAC-таблица,
  *                                   DevType 2/3/4) + Config_common.
  *                                   Devices_Expected (конфигуратор);
  *            downlink            -> бит ST_LORA_BIT_EVACUATION (эвакуация) и
  *                                   блок Message (п.7.1 протокола) - текст
  *                                   классифицируется здесь (Alert_LoraMessage).
  *
  *          НЕПОДВИЖНОСТЬ - как в ТЗ:
  *            - функция включается в настройках (Immobility_Enable);
  *            - нет движения Immobility_Window_s -> СТАТУС + индикация на
  *              дисплее, звук и свет;
  *            - одновременно стартует предаварийный таймер
  *              Immobility_PreAlarm_s («время начать движение или снять
  *              индикацию»);
  *            - встряхивание/начало движения в пределах таймера -> статус
  *              снимается, инцидент НЕ отправляется;
  *            - реакции нет -> инцидент «Неподвижность/Отсутствие активности»
  *              на сервер (Motion.MOTION_BIT_INACTIVE + EventLog 0x10), статус
  *              остаётся до появления движения;
  *            - индикацию можно снять вручную (квитирование) - но статус и
  *              таймер от этого НЕ останавливаются (безопасность: квитирование
  *              не отменяет инцидент).
  *
  *  ============================ ЧАСТЬ 2. ДВИЖОК =============================
  *          НЕБЛОКИРУЮЩИЙ секвенсор оповещений (порт модели Avis): в Avis
  *          включение прибора мигало вибро/светом/звуком через блокирующие
  *          Delay() (пример автора в Button_Run_Start). В RTOS так нельзя -
  *          поток LVGL Task встанет. Здесь сценарий описывается КОНСТАНТНОЙ
  *          таблицей фаз, а движок Alert_IndicationTick() (~каждые 10 мс из
  *          потока LVGL Task, main_vector.c) сам включает/выключает каналы по
  *          дедлайнам VTICK - без единой задержки в вызывающем коде.
  *
  *          Модель (указания автора: «свет, звук и вибро - отдельными фазами»,
  *          «включение - как в Avis», «у тревог задаются период и число
  *          повторов»): последовательность (alert_seq_t) = массив ФАЗ; фаза =
  *          МАСКА каналов + время включения (период) + пауза + число повторов.
  *          Фазы идут строго по порядку - порядок строк таблицы и есть порядок
  *          оповещения. Маска позволяет и одновременные каналы ОДНОЙ фазой
  *          (как в Avis: звук+вибро+свет стартуют вместе) - просто укажите
  *          нужные биты через «|» (см. AlertSeq_PowerOn).
  *
  *          Каналы (alert_ch_t - БИТОВАЯ МАСКА, сочетаются через «|»):
  *            ALERT_CH_LED1  0x01 - ALARM_LED_1 (PA11, SET_ON/SET_OFF);
  *            ALERT_CH_LED2  0x02 - ALARM_LED_2 (PA12);
  *            ALERT_CH_VIBRO 0x04 - хуки Vibro_On()/Vibro_Off() (vector_macros,
  *                                 weak-заглушки; когда мотор будет разведён -
  *                                 заменить тело, пример Avis TMR4 CH1 в
  *                                 комментарии vector_macros.c);
  *            ALERT_CH_BEEP  0x08 - audio_beep(): писк 240 мс из КОНСТ-массива
  *                                 во ВНУТРЕННЕЙ flash (audio_beep.c). Не
  *                                 зависит от внешней SPI-flash - поэтому он
  *                                 и для включения прибора, и тон тревог по
  *                                 умолчанию;
  *            ALERT_CH_SOUND 0x10 - audio_play_now(sound_idx): клип образа
  *                                 внешней flash (SND_* из audio_ids.h),
  *                                 звуковой индекс фазы - поле sound_idx.
  *
  *          Тайминги фазы:
  *            t_on_ms  - период: сколько каналы включены; для BEEP/SOUND
  *                       допустимо ALERT_WAIT_END (0xFFFF) = «ждать конец
  *                       воспроизведения» (audio_is_busy()), с защитной
  *                       выдержкой ALERT_WAIT_GRACE_MS на старт очереди плеера;
  *            t_off_ms - пауза после выключения (0 = сразу следующая фаза);
  *            repeats  - сколько раз повторить фазу ВКЛ/ВЫКЛ (0 и 1 = один раз);
  *            blink    - 1 = «мигать» светодиодами фазы с периодом
  *                       ALERT_BLINK_PERIOD_MS (100 мс, ритм SET_TGL из
  *                       LedSoundVibroPlay Avis) вместо ровного света;
  *            sound_idx- клип SND_* для канала SOUND (остальным каналам
  *                       безразличен: для BEEP индекс НЕ используется).
  *
  *          Повторы ВСЕЙ последовательности: поле count сценария
  *          (0 = ALERT_REPEAT_FOREVER - крутить до Alert_Stop()/вытеснения,
  *          как прежний loop=1 для тревог; 1 = один прогон). Для статусов
  *          период/пауза/мигание/повторы берутся из таблицы s_desc, для
  *          разовых сигналов - из вызова Alert_SignalEx() (значения 0 =
  *          «взять из таблицы s_sig_tab», а она - порт индикации Avis:
  *          LedSoundVibroPlay(id, pulse, pause, blink, block_sound, block_led,
  *          test_sound), единицы pulse/pause - 100 мс).
  *
  *          Приоритеты: Alert_Start() новой последовательности С ПРАВОМ на
  *          вытеснение: если уже идёт seq с prio ВЫШЕ - новая отклоняется
  *          (возврат из Tick в лог); равный/больший prio - текущая гасится
  *          (каналы выключаются) и начинается новая. Тревога (ALERT_PRIO_ALARM)
  *          вытеснит сервисные писки (ALERT_PRIO_UI).
  *
  *          ПОТОКИ: Alert_IndicationTick() - ТОЛЬКО поток LVGL Task
  *          (единственный владелец движка). Alert_Tick() (реестр статусов) -
  *          Measure Task, 100 мс. Alert_Start/StartEx/Signal/SignalEx/Stop,
  *          Alert_Set/Clear/AckButton - ИЗ ЛЮБОГО потока (пишут только
  *          volatile-запрос; движок подхватит его в ближайший Tick, т.е.
  *          старт/стоп с точностью ~10 мс). Из ISR не звать.
  *
  *          Память: статикой ~120 байт; таблицы сценариев и статусов - const
  *          во flash.
  ******************************************************************************
  */
#ifndef ALERT_VECTOR_H
#define ALERT_VECTOR_H

#include <stdint.h>
#include "config_device.h"   /* CONFIG_LORA / CONFIG_BLE / CONFIG_LIS3DH */

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 *                 ЧАСТЬ 2 (нижний уровень): ДВИЖОК ИНДИКАЦИИ
 * ============================================================================ */

/* «Ждать конец воспроизведения» вместо таймера (только для BEEP/SOUND) и
   защитная выдержка на старт очереди плеера.                                */
#define ALERT_WAIT_END           ((uint16_t)0xFFFFu)
#define ALERT_WAIT_GRACE_MS      (80u)

/* Период мигания LED фазы blink=1 (ритм SET_TGL в LedSoundVibroPlay Avis:
   переключение каждые 100 мс - тик их Indication()).                        */
#define ALERT_BLINK_PERIOD_MS    (100u)

/* Единицы period_ms/pause_ms в Alert_SignalEx - как time_pulse/time_pause в
   LedSoundVibroPlay Avis (тик 100 мс). Значение 0 = «по умолчанию из таблицы»
   (для pause_ms это законный «без паузы» - см. доки .c).                    */
#define ALERT_SIG_UNIT_MS        (100u)

/* Повторы ВСЕЙ последовательности: 0 = вечно (до Alert_Stop/вытеснения).     */
#define ALERT_REPEAT_FOREVER     (0u)

/* Приоритеты последовательностей (больший вытесняет меньший/равный).        */
#define ALERT_PRIO_SERVICE       (0u)   /* фоновые/сервисные                   */
#define ALERT_PRIO_UI            (1u)   /* действия пользователя (включение)   */
#define ALERT_PRIO_ALARM         (2u)   /* тревоги (газ, SOS, батарея)         */

/* Каналы фазы - БИТОВАЯ МАСКА: каналы одной фазы стартуют одновременно
   (Avis LedSoundVibroPlay: SOUND_ON+VIBRO_ON+LED в одном такте).            */
typedef enum
{
  ALERT_CH_LED1  = 0x01,   /* ALARM_LED_1 (PA11)                            */
  ALERT_CH_LED2  = 0x02,   /* ALARM_LED_2 (PA12)                            */
  ALERT_CH_VIBRO = 0x04,   /* Vibro_On/Off (хук vector_macros, weak)        */
  ALERT_CH_BEEP  = 0x08,   /* audio_beep(): 240 мс, внутренняя flash        */
  ALERT_CH_SOUND = 0x10    /* audio_play_now(sound_idx): клип образа (SND_*)*/
} alert_ch_t;

/* Идентификаторы готовых СИГНАЛОВ - порт switch_flag_led из LedSoundVibroPlay/
   Indication (Avis_Optional_Functions.c). Период/пауза/мигание каждого -
   таблица s_sig_tab в Alert_vector.c (значения Avis), старт - Alert_Signal().
   Это разовые сигналы (СОБЫТИЯ), а не статусы: SOS/групповой/зарядка/тест.
   Сигналы SYSERR/LOWBAT/SENSFAIL из Avis теперь ведёт реестр статусов
   (ALERT_DEVICE_ERROR / ALERT_BATTERY_*), таблица сохранена для совместимости
   вызовов и для ручного теста.                                              */
#define ALERT_SIG_SYSERR         (0x01u)  /* ошибка системы: 1 с / 0.5 с, LED  */
#define ALERT_SIG_EXCEEDED       (0x02u)  /* выход за диапазон: 1 с / 0.2 с    */
#define ALERT_SIG_LIMIT3         (0x03u)  /* порог 3 (TWA/STEL): 0.5 с / 0.3 с */
#define ALERT_SIG_LIMIT2         (0x04u)  /* порог 2: 0.5 с / 0.5 с            */
#define ALERT_SIG_LIMIT1         (0x05u)  /* порог 1: 0.6 с / 0.9 с, мигание   */
#define ALERT_SIG_LOWBAT         (0x06u)  /* низкая батарея: 0.2 с / 0.3 с     */
#define ALERT_SIG_SENSFAIL       (0x07u)  /* отказ датчика: 1 с / 0.5 с, LED   */
#define ALERT_SIG_SOS            (0x08u)  /* SOS (LoRa): 0.6 с / 0.4 с, миг.   */
#define ALERT_SIG_GROUP          (0x09u)  /* групповой (LoRa): 1 с / 0.5 с     */
#define ALERT_SIG_BACKGR         (0x0Au)  /* фоновый (LoRa): 1 с / 0.2 с, миг. */
#define ALERT_SIG_CHARGE         (0x0Bu)  /* зарядка звук+вибро: 0.6 с / 0.2 с */
#define ALERT_SIG_TEST           (0x0Fu)  /* тест звука: 0.5 с / 1 с, без LED  */

/* Одна фаза: МАСКА каналов + период (ВКЛ) + пауза + повторы. Примеры:
     { ALERT_CH_VIBRO, 500u, 200u, 1u, 0u, 0u }  - вибро 0.5 с, пауза 0.2 с;
     { (uint8_t)(ALERT_CH_BEEP | ALERT_CH_LED1),
       200u, 200u, 1u, 0u, 0u }                   - писк+LED1 ОДНОВРЕМЕННО
                                                    (как «короткий сигнал»
                                                    включения в Avis);
     { ALERT_CH_SOUND, ALERT_WAIT_END, 0u, 1u, 0u, SND_KOLOKOL_1 }
                                                  - голос/звук из образа,
                                                    ждать конец клипа.        */
typedef struct
{
  uint8_t  ch;          /* маска alert_ch_t (LED1|LED2|VIBRO|BEEP|SOUND)     */
  uint16_t t_on_ms;     /* период включения; ALERT_WAIT_END - ждать плеер    */
  uint16_t t_off_ms;    /* пауза после выключения (0 = без паузы)            */
  uint8_t  repeats;     /* повторов фазы ВКЛ/ВЫКЛ (0 и 1 = один раз)         */
  uint8_t  blink;       /* 1 = мигать LED-битами фазы (100 мс), а не светить */
  uint16_t sound_idx;   /* для бита CH_SOUND: SND_* (audio_ids.h); прочим - 0*/
} alert_phase_t;

/* Последовательность фаз (сценарий оповещения). count - сколько раз прогнать
   ВСЮ последовательность: ALERT_REPEAT_FOREVER (0) = до Alert_Stop() /
   вытеснения (тревога до подтверждения); 1 = один прогон.                    */
typedef struct
{
  const alert_phase_t *ph;   /* таблица фаз (const, во flash)                */
  uint8_t  n;                /* число фаз (>=1)                              */
  uint8_t  prio;             /* ALERT_PRIO_*                                 */
  uint8_t  count;            /* повторов всей последовательности (0 = вечно) */
} alert_seq_t;

/* Готовый сценарий включения прибора (порт Button_Run_Start из Avis).        */
extern const alert_seq_t AlertSeq_PowerOn;

/* --- API движка -----------------------------------------------------------*/
/* Запрос старта сценария: любой поток; старт в ближайший Alert_IndicationTick
   (~10 мс); prio решает, вытеснит ли текущую.                              */
void    Alert_Start(const alert_seq_t *seq);
/* То же, но с ЗАДАВАЕМЫМИ периодом/паузой/повторами первой фазы и числом
   прогонов (0 в period/pause/repeats = значение из таблицы; 0 в count =
   ALERT_REPEAT_FOREVER).                                                    */
void    Alert_StartEx(const alert_seq_t *seq,
                      uint16_t period_ms,  /* 0 = t_on_ms из таблицы         */
                      uint16_t pause_ms,   /* 0 = t_off_ms из таблицы        */
                      uint8_t  repeats,    /* 0 = repeats из таблицы         */
                      uint8_t  count);     /* 0 = ВЕЧНО (до Alert_Stop)      */
/* Разовый сигнал ALERT_SIG_* с параметрами по умолчанию из таблицы s_sig_tab
   (1 пачка) либо с заданными (Alert_SignalEx: 0 = «по умолчанию», count = 0 -
   вечно, до Alert_Stop).                                                    */
void    Alert_Signal(uint8_t sig_id);
void    Alert_SignalEx(uint8_t sig_id,
                       uint16_t period_ms, uint16_t pause_ms,
                       uint8_t  blink,     /* 1 = мигать LED (100 мс)        */
                       uint8_t  count);    /* повторов пачки; 0 = вечно      */
void    Alert_Stop(void);                  /* запрос стопа: любой поток;
                                              каналы погасятся в Tick        */
uint8_t Alert_Busy(void);                  /* 1 = что-то идёт/запрошено      */
/* Движок: поток LVGL Task ~10 мс (был Notify_Tick).                        */
void    Alert_IndicationTick(void);

/* ============================================================================
 *                 ЧАСТЬ 1 (верхний уровень): РЕЕСТР СТАТУСОВ
 * ============================================================================ */

/* Идентификаторы статусов/тревог. Порядок = приоритет показа на экране и
   перехвата индикации (чем МЕНЬШЕ номер, тем выше приоритет): сначала угроза
   жизни (газ, эвакуация, неподвижность), затем комплект (нет ГА/СИЗ/браслета),
   затем прибор (критический разряд, ошибки модулей, низкий заряд), затем
   сообщение.                                                                */
typedef enum
{
  ALERT_NONE = 0,
  ALERT_GAS_ALARM,        /* «ОСТОРОЖНО! ГАЗ!» (пороги Avis или сервер, ID) */
  ALERT_EVACUATION,       /* «ВСЕМ НА ВЫХОД» (бит EVACUATION от сервера)    */
  ALERT_IMMOBILITY,       /* «Неподвижность/Отсутствие активности»          */
  ALERT_NO_GAS,           /* нет газоанализатора (ГА) в комплекте           */
  ALERT_NO_PPE,           /* нет СИЗ                                        */
  ALERT_NO_BRACELET,      /* нет браслета (пульсометра)                     */
  ALERT_BATTERY_CRIT,     /* критический разряд батареи                     */
  ALERT_DEVICE_ERROR,     /* ошибка прибора (модули: ТЕМП/NB-IoT/BLE/GPS/
                             GSM/LORA/LCD), param = номер бита StateErr     */
  ALERT_BATTERY_LOW,      /* низкий заряд АКБ                               */
  ALERT_MESSAGE,          /* текстовое сообщение сервера (Message, п.7.1)   */
  ALERT_ID_COUNT
} alert_id_t;

/* Биты маски ожидаемых устройств: статус «нет X» взводится ТОЛЬКО если
   устройство ожидается. Маска = Config_common.Devices_Expected (конфигуратор)
   ИЛИ Ble_ExpectedDevices() (MAC-таблица BLE-модуля, DevType 2/3/4 - решение
   автора «автоматически по MAC-таблице»). Снятие статуса - связь
   восстановилась (данные свежие) ИЛИ устройство удалено из таблицы/настроек
   (бит сброшен) - требование автора п.3.                                    */
#define ALERT_DEV_EXPECT_PPE       (0x0001u)
#define ALERT_DEV_EXPECT_GAS       (0x0002u)
#define ALERT_DEV_EXPECT_BRACELET  (0x0004u)

/* Биты Config_common.Alert_Flags.                                           */
#define ALERT_CFG_MASTER           (0x0001u)  /* трекер-«мастер»: может сам
                                                  отправить «ВСЕМ НА ВЫХОД» */
#define ALERT_CFG_IMMOB_SOUND      (0x0002u)  /* звук при неподвижности
                                                  (0 = только свет/вибро)   */

/* Severity инцидента LoRa (п.4.8 протокола v4).                              */
#define ALERT_SEV_INFO             (0u)
#define ALERT_SEV_WARNING          (1u)
#define ALERT_SEV_CRITICAL         (2u)

/* ---------------------------------------------------------------------------
 * API реестра
 * -------------------------------------------------------------------------*/

/* Сброс реестра И движка. КОНТЕКСТ: Measure Task (Vector_Run_Pre_Init), до
   первого Alert_Tick()/Alert_IndicationTick().                              */
void Alert_Init(void);

/* Взвести/снять СТАТУС (индикация стартует сама в Alert_Tick). Пока условие
   подтверждается, Alert_Set() можно звать периодически - она обновляет метку
   времени (по ней работает автоочистка hold_s), индикацию НЕ перезапуская.
   КОНТЕКСТ: любой поток.                                                    */
void Alert_Set(alert_id_t id);
void Alert_Clear(alert_id_t id);

/* 1 = статус активен; 1 = индикация статуса сейчас идёт (не квитирована и не
   отработала свою однократную пачку).                                       */
uint8_t Alert_IsActive(alert_id_t id);
uint8_t Alert_Indication(alert_id_t id);

/* Квитирование ЗЕЛЁНОЙ кнопкой: гасит индикацию активного статуса с
   ack_button = 1 (сам статус и его таймеры НЕ трогаются). Возврат: id
   квитированного статуса или ALERT_NONE (нечего квитировать - жест уходит в
   навигацию меню). КОНТЕКСТ: поток LVGL Task (Menu_Run).                    */
uint8_t Alert_AckButton(void);

/* «ОСТОРОЖНО! ГАЗ!» с ID трекера-источника (требование автора п.4: на
   дисплее виден ID сотрудника, у которого сработал газоанализатор).
   Источники: пороги своего Avis (внутренний gas_local_tick, ID = свой) и
   сообщение сервера (Alert_LoraMessage, ID из текста «... ID 1234»).
   voice_text - не нужен: голосовой клип берётся из ALERT_SND_GAS.           */
void Alert_GasAlarm(uint16_t source_tracker_id);

/* Текстовое сообщение сервера (блок Message, LoRa v4 п.7.1): текст CP1251
   конвертируется в UTF-8 для LVGL и классифицируется - «ОСТОРОЖНО! ГАЗ!» /
   «ВСЕМ НА ВЫХОД» поднимают соответствующий статус (с голосом), прочее -
   ALERT_MESSAGE (сигнал сообщения). seq нужен для квитирования доставки
   (MsgAck) и отсечения дублей. Возврат: id поднятого статуса.               */
uint8_t Alert_LoraMessage(const uint8_t *text_cp1251, uint8_t len, uint8_t seq);

/* «ВСЕМ НА ВЫХОД» С САМОГО трекера (режим «мастер», ALERT_CFG_MASTER):
   взводит локальный статус и, при CONFIG_LORA, ставит бит
   ST_LORA_BIT_EVACUATION в ControlLora16 (сервер разносит остальным).
   Возврат: 1 = отправлено/взведено, 0 = режим мастера выключен.             */
uint8_t Alert_SendEvacuation(void);

/* Движок реестра: периодика ALERT_TICK_MS (100 мс) из Measure Task. Внутри:
   разбор событий движения и downlink-сообщения LoRa; раз в секунду - счёт
   неподвижности (окно + предаварийный таймер + инцидент), газовые пороги
   своего Avis, свежесть устройств комплекта (СИЗ/ГА/браслет), ошибки прибора
   и батарея, автоочистка статусов с одноразовым источником (hold_s), зеркало
   бита эвакуации и синхронизация Motion-блока LoRa; каждый такт - арбитраж
   индикации (один лидер) через движок ЧАСТИ 2.                              */
void Alert_Tick(void);

/* Экран/диагностика.                                                        */
alert_id_t  Alert_Top(void);            /* приоритетный активный статус      */
const char *Alert_Name(alert_id_t id);  /* ключ i18n (RU-литерал)            */
uint16_t    Alert_Param(alert_id_t id); /* GAS: ID трекера; MESSAGE: MsgSeq;
                                           DEVICE_ERROR: номер бита StateErr */
/* Подробное имя для статуса с параметром: ALERT_DEVICE_ERROR -> имя конкретной
   ошибки («Ошибка BLE»), иначе пустая строка. Ключ i18n.                    */
const char *Alert_ParamText(alert_id_t id);
/* Имя ошибки прибора по номеру бита Config_common.StateErr (enum ST_COMMON_ERR)
   - ЕДИНСТВЕННЫЙ источник имён: его использует и слой экрана
   (TFT_indicator.c, категория 1 очереди ошибок), и лог. Вне диапазона/пустое
   имя -> "" (такой бит на экране не показывается). Ключ i18n.               */
const char *Alert_SysErrName(uint8_t bit);
const char *Alert_MessageText(void);    /* UTF-8 текст сообщения сервера     */
uint8_t     Alert_AnyActive(void);      /* 1 = есть активные статусы         */

#ifdef __cplusplus
}
#endif

#endif /* ALERT_VECTOR_H */
