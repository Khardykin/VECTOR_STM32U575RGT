/**
  ******************************************************************************
  * @file    Notify_vector.h
  * @brief   НЕБЛОКИРУЮЩИЙ секвенсор оповещений: светодиоды / звук / вибро
  *          по ФАЗАМ в нужном порядке и с нужными таймингами.
  *
  *          Зачем: в Avis включение прибора мигало вибро/светом/звуком через
  *          блокирующие Delay() (пример автора в Button_Run_Start). В RTOS так
  *          нельзя - поток LVGL Task встанет. Здесь тот же сценарий описывается
  *          КОНСТАНТНОЙ таблицей фаз, а движок Notify_Tick() (~каждые 10 мс из
  *          потока LVGL Task, main_vector.c) сам включает/выключает каналы
  *          по дедлайнам VTICK - без единой задержки в вызывающем коде.
  *
  *          Модель (указание автора: «свет, звук и вибро - отдельными фазами»,
  *          «включение - как в Avis», «у тревог задаются период и число
  *          повторов»): последовательность (notify_seq_t) = массив ФАЗ; фаза =
  *          МАСКА каналов + время включения (период) + пауза + число повторов.
  *          Фазы идут строго по порядку - порядок строк таблицы и есть порядок
  *          оповещения. Маска позволяет и одновременные каналы ОДНОЙ фазой
  *          (как в Avis: звук+вибро+свет стартуют вместе) - просто укажите
  *          нужные биты через «|» (см. NotifySeq_PowerOn).
  *
  *          Каналы (notify_ch_t - БИТОВАЯ МАСКА, сочетаются через «|»):
  *            NOTIFY_CH_LED1  0x01 - ALARM_LED_1 (PA11, SET_ON/SET_OFF);
  *            NOTIFY_CH_LED2  0x02 - ALARM_LED_2 (PA12);
  *            NOTIFY_CH_VIBRO 0x04 - хуки Vibro_On()/Vibro_Off() (vector_macros,
  *                                 weak-заглушки; когда мотор будет разведён -
  *                                 заменить тело, пример Avis TMR4 CH1 в
  *                                 комментарии vector_macros.c);
  *            NOTIFY_CH_BEEP  0x08 - audio_beep(): писк 240 мс из КОНСТ-массива
  *                                 во ВНУТРЕННЕЙ flash (audio_beep.c). Не
  *                                 зависит от внешней SPI-flash - поэтому он
  *                                 и для включения прибора, и тон тревог по
  *                                 умолчанию;
  *            NOTIFY_CH_SOUND 0x10 - audio_play_now(sound_idx): клип образа
  *                                 внешней flash (SND_* из audio_ids.h),
  *                                 звуковой индекс фазы - поле sound_idx.
  *
  *          Тайминги фазы:
  *            t_on_ms  - период: сколько каналы включены; для BEEP/SOUND
  *                       допустимо NOTIFY_WAIT_END (0xFFFF) = «ждать конец
  *                       воспроизведения» (audio_is_busy()), с защитной
  *                       выдержкой NOTIFY_WAIT_GRACE_MS на старт очереди
  *                       плеера;
  *            t_off_ms - пауза после выключения (0 = сразу следующая фаза);
  *            repeats  - сколько раз повторить фазу ВКЛ/ВЫКЛ (0 и 1 = один раз);
  *            blink    - 1 = «мигать» светодиодами фазы с периодом
  *                       NOTIFY_BLINK_PERIOD_MS (100 мс, ритм SET_TGL из
  *                       LedSoundVibroPlay Avis) вместо ровного света;
  *            sound_idx- клип SND_* для канала SOUND (остальным каналам
  *                       безразличен - раньше в таблице включения стоял 0 и
  *                       это сбивало с толку: для BEEP индекс НЕ используется,
  *                       0 «звучал» бы как SND_KOLOKOL_1 только в фазе SOUND).
  *
  *          Повторы ВСЕЙ последовательности: поле count сценария
  *          (0 = NOTIFY_REPEAT_FOREVER - крутить до Notify_Stop()/вытеснения,
  *          как прежний loop=1 для тревог; 1 = один прогон). Для тревог
  *          период/пауза/мигание/повторы ЗАДАЮТСЯ вызовом Notify_AlarmEx() -
  *          значения 0 означают «взять из таблицы по умолчанию» (таблица -
  *          порт индикации Avis: LedSoundVibroPlay(id, pulse, pause, blink,
  *          block_sound, block_led, test_sound), единицы pulse/pause - 100 мс).
  *
  *          Приоритеты: Notify_Start() новой последовательности С ПРАВОМ на
  *          вытеснение: если уже идёт seq с prio ВЫШЕ - новая отклоняется
  *          (возврат из Tick в лог); равный/больший prio - текущая гасится
  *          (каналы выключаются) и начинается новая. Тревога (PRIO_ALARM)
  *          вытеснит сервисные писки (PRIO_UI).
  *
  *          ПОТОКИ: Notify_Start(), Notify_StartEx(), Notify_Alarm(),
  *          Notify_AlarmEx() и Notify_Stop() - ИЗ ЛЮБОГО
  *          потока (пишут только volatile-запрос; движок подхватит его в
  *          ближайший Tick, т.е. старт/стоп с точностью ~10 мс). Notify_Tick()
  *          - ТОЛЬКО поток LVGL Task (единственный владелец движка).
  *          Из ISR не звать.
  *
  *          Память: статикой ~48 байт; таблицы сценариев - const во flash.
  ******************************************************************************
  */
#ifndef NOTIFY_VECTOR_H
#define NOTIFY_VECTOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* t_on_ms фазы BEEP/SOUND: ждать окончания воспроизведения (audio_is_busy),
   а не таймер. Защитная выдержка на старт очереди плеера - в .c.            */
#define NOTIFY_WAIT_END        ((uint16_t)0xFFFFu)

/* count сценария / число повторов тревоги: крутить до Notify_Stop() или
   вытеснения более приоритетным сценарием (прежнее поле loop=1).            */
#define NOTIFY_REPEAT_FOREVER  (0u)

/* Период мигания светодиодов фазы blink=1 (ритм SET_TGL в LedSoundVibroPlay
   Avis: переключение каждые 100 мс - тик их Indication()).                  */
#define NOTIFY_BLINK_PERIOD_MS (100u)

/* Единицы period_ms/pause_ms в Notify_AlarmEx - как time_pulse/time_pause в
   LedSoundVibroPlay Avis (тик 100 мс). Значение 0 = «по умолчанию из
   таблицы» (для pause_ms это законный «без паузы» - см. доки .c).           */
#define NOTIFY_ALARM_UNIT_MS   (100u)

/* Приоритеты последовательностей (больший вытесняет меньший/равный).        */
#define NOTIFY_PRIO_SERVICE    (0u)   /* фоновые/сервисные                   */
#define NOTIFY_PRIO_UI         (1u)   /* действия пользователя (включение)   */
#define NOTIFY_PRIO_ALARM      (2u)   /* тревоги (газ, SOS, батарея)         */

/* Каналы фазы - БИТОВАЯ МАСКА: каналы одной фазы стартуют одновременно
   (Avis LedSoundVibroPlay: SOUND_ON+VIBRO_ON+LED в одном такте).            */
typedef enum
{
  NOTIFY_CH_LED1  = 0x01,   /* ALARM_LED_1 (PA11)                            */
  NOTIFY_CH_LED2  = 0x02,   /* ALARM_LED_2 (PA12)                            */
  NOTIFY_CH_VIBRO = 0x04,   /* Vibro_On/Off (хук vector_macros, weak)        */
  NOTIFY_CH_BEEP  = 0x08,   /* audio_beep(): 240 мс, внутренняя flash        */
  NOTIFY_CH_SOUND = 0x10    /* audio_play_now(sound_idx): клип образа (SND_*)*/
} notify_ch_t;

/* Идентификаторы тревог - порт switch_flag_led из LedSoundVibroPlay/Indication
   (Avis_Optional_Functions.c). Период/пауза/мигание каждого - таблица
   s_alarm_tab в Notify_vector.c (значения Avis), старт - Notify_Alarm().    */
#define NOTIFY_ALARM_SYSERR    (0x01u)  /* ошибка системы: 1 с / 0.5 с, LED  */
#define NOTIFY_ALARM_EXCEEDED  (0x02u)  /* выход за диапазон: 1 с / 0.2 с    */
#define NOTIFY_ALARM_LIMIT3    (0x03u)  /* порог 3 (TWA/STEL): 0.5 с / 0.3 с */
#define NOTIFY_ALARM_LIMIT2    (0x04u)  /* порог 2: 0.5 с / 0.5 с            */
#define NOTIFY_ALARM_LIMIT1    (0x05u)  /* порог 1: 0.6 с / 0.9 с, мигание   */
#define NOTIFY_ALARM_LOWBAT    (0x06u)  /* низкая батарея: 0.2 с / 0.3 с     */
#define NOTIFY_ALARM_SENSFAIL  (0x07u)  /* отказ датчика: 1 с / 0.5 с, LED   */
#define NOTIFY_ALARM_SOS       (0x08u)  /* SOS (LoRa): 0.6 с / 0.4 с, миг.   */
#define NOTIFY_ALARM_GROUP     (0x09u)  /* групповой (LoRa): 1 с / 0.5 с     */
#define NOTIFY_ALARM_BACKGR    (0x0Au)  /* фоновый (LoRa): 1 с / 0.2 с, миг. */
#define NOTIFY_ALARM_CHARGE    (0x0Bu)  /* зарядка звук+вибро: 0.6 с / 0.2 с */
#define NOTIFY_ALARM_TEST      (0x0Fu)  /* тест звука: 0.5 с / 1 с, без LED  */

/* Одна фаза: МАСКА каналов + период (ВКЛ) + пауза + повторы. Примеры:
     { NOTIFY_CH_VIBRO, 500u, 200u, 1u, 0u, 0u }  - вибро 0.5 с, пауза 0.2 с;
     { (notify_ch_t)(NOTIFY_CH_BEEP | NOTIFY_CH_LED1),
       200u, 200u, 1u, 0u, 0u }                   - писк+LED1 ОДНОВРЕМЕННО
                                                    (как «короткий сигнал»
                                                    включения в Avis);
     { NOTIFY_CH_SOUND, NOTIFY_WAIT_END, 0u, 1u, 0u, SND_KOLOKOL_1 }
                                                  - голос/звук из образа,
                                                    ждать конец клипа.        */
typedef struct
{
  uint8_t  ch;          /* маска notify_ch_t (LED1|LED2|VIBRO|BEEP|SOUND)    */
  uint16_t t_on_ms;     /* период включения; NOTIFY_WAIT_END - ждать плеер   */
  uint16_t t_off_ms;    /* пауза после выключения (0 = без паузы)            */
  uint8_t  repeats;     /* повторов фазы ВКЛ/ВЫКЛ (0 и 1 = один раз)         */
  uint8_t  blink;       /* 1 = мигать LED-битами фазы (100 мс), а не светить */
  uint16_t sound_idx;   /* для бита CH_SOUND: SND_* (audio_ids.h); прочим - 0*/
} notify_phase_t;

/* Последовательность фаз (сценарий оповещения). count - сколько раз прогнать
   ВСЮ последовательность: NOTIFY_REPEAT_FOREVER (0) = до Notify_Stop() /
   вытеснения (тревога до подтверждения); 1 = один прогон.                   */
typedef struct
{
  const notify_phase_t *ph;    /* таблица фаз (const, во flash)              */
  uint8_t  n;                  /* число фаз (>=1)                            */
  uint8_t  prio;               /* NOTIFY_PRIO_*                              */
  uint8_t  count;              /* повторов всей последовательности (0 = вечно)*/
} notify_seq_t;

/* ---------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------*/
void    Notify_Init(void);                  /* сброс движка (Menu_Init)      */

void    Notify_Start(const notify_seq_t *seq); /* запрос старта сценария:
                                                  любой поток; старт в ближайший
                                                  Tick (~10 мс); prio решает,
                                                  вытеснит ли текущую         */
void    Notify_StartEx(const notify_seq_t *seq,
                       uint16_t period_ms,  /* 0 = t_on_ms из таблицы        */
                       uint16_t pause_ms,   /* 0 = t_off_ms из таблицы       */
                       uint8_t  repeats,    /* 0 = repeats из таблицы        */
                       uint8_t  count);     /* 0 = ВЕЧНО (до Notify_Stop)    */
void    Notify_Alarm(uint8_t alarm_id);     /* тревога NOTIFY_ALARM_* с
                                                 параметрами ПО УМОЛЧАНИЮ
                                                 (таблица - порт Avis):
                                                 1 пачка, звук+вибро+LED вместе*/
void    Notify_AlarmEx(uint8_t alarm_id,
                       uint16_t period_ms,  /* период пачки, мс; 0 = по умолч.
                                               (pulse*100 из таблицы Avis)     */
                       uint16_t pause_ms,   /* пауза, мс; 0 = по умолчанию
                                               (pause*100 из таблицы Avis)     */
                       uint8_t  blink,      /* 1 = мигать LED (100 мс, как
                                               mode_blink_led Avis)            */
                       uint8_t  count);     /* повторов пачки; 0 = ВЕЧНО, до
                                               Notify_Stop (тревога до
                                               подтверждения)                  */
void    Notify_Stop(void);                  /* запрос стопа: любой поток;
                                                каналы погасятся в Tick      */
uint8_t Notify_Busy(void);                  /* 1 = что-то идёт/запрошено      */
void    Notify_Tick(void);                  /* движок: поток LVGL Task ~10 мс */

/* Готовые сценарии (таблицы в Notify_vector.c).                             */
extern const notify_seq_t NotifySeq_PowerOn;   /* включение - порт Avis:
                                                  вибро 0.5 с -> писк+LED1
                                                  0.2 с -> писк+LED2 0.8 с    */

#ifdef __cplusplus
}
#endif

#endif /* NOTIFY_VECTOR_H */
