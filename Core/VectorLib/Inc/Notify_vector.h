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
  *          потока LVGL Task, app_azure_rtos.c) сам включает/выключает каналы
  *          по дедлайнам VTICK - без единой задержки в вызывающем коде.
  *
  *          Модель (ответ автору: "свет, потом звук, потом вибро, или в другом
  *          порядке"): последовательность (notify_seq_t) = массив ФАЗ; фаза =
  *          канал + время включения + пауза + число повторов. Фазы идут строго
  *          по порядку - порядок каналов в таблице и есть порядок оповещения.
  *          Голосовые сообщения - та же фаза SOUND (клип образа внешней flash),
  *          просто без соседей по таблице ("без фаз").
  *
  *          Каналы:
  *            NOTIFY_CH_LED1/LED2 - ALARM_LED_1/2 (PA11/PA12, SET_ON/SET_OFF);
  *            NOTIFY_CH_VIBRO     - хуки Vibro_On()/Vibro_Off() (vector_macros,
  *                                  weak-заглушки; когда мотор будет разведён -
  *                                  заменить тело, пример Avis TMR4 CH1 в
  *                                  комментарии vector_macros.c);
  *            NOTIFY_CH_BEEP      - audio_beep(): писк 240 мс из КОНСТ-массива
  *                                  во ВНУТРЕННЕЙ flash (audio_beep.c). Не
  *                                  зависит от внешней SPI-flash - поэтому он
  *                                  и для включения прибора;
  *            NOTIFY_CH_SOUND     - audio_play_now(sound_idx): клип из образа
  *                                  внешней flash (SND_* из audio_ids.h).
  *
  *          Тайминги фазы:
  *            t_on_ms  - сколько канал включён; для BEEP/SOUND допустимо
  *                       NOTIFY_WAIT_END (0xFFFF) = "ждать конец воспроизве-
  *                       дения" (audio_is_busy()), с защитной выдержкой
  *                       NOTIFY_WAIT_GRACE_MS на старт очереди плеера;
  *            t_off_ms - пауза после выключения (0 = сразу следующая фаза);
  *            repeats  - сколько раз повторить фазу ВКЛ/ВЫКЛ (>=1).
  *
  *          Приоритеты: Notify_Start() новой последовательности С ПРАВОМ на
  *          вытеснение: если уже идёт seq с prio ВЫШЕ - новая отклоняется
  *          (возврат из Tick в лог); равный/больший prio - текущая гасится
  *          (каналы выключаются) и начинается новая. Тревога (PRIO_ALARM)
  *          вытеснит сервисные писки (PRIO_UI) - база под будущие тревоги.
  *
  *          ПОТОКИ: Notify_Start()/Notify_Stop() - ИЗ ЛЮБОГО потока/ISR-безопасно
  *          (пишут только volatile-запрос; движок подхватит его в ближайший
  *          Tick, т.е. старт/стоп с точностью ~10 мс). Notify_Tick() - ТОЛЬКО
  *          поток LVGL Task (единственный владелец движка). Из ISR не звать.
  *
  *          Память: статикой ~40 байт; таблицы сценариев - const во flash.
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

/* Приоритеты последовательностей (больший вытесняет меньший/равный).        */
#define NOTIFY_PRIO_SERVICE    (0u)   /* фоновые/сервисные                   */
#define NOTIFY_PRIO_UI         (1u)   /* действия пользователя (включение)   */
#define NOTIFY_PRIO_ALARM      (2u)   /* тревоги (будущее: газ, SOS)         */

/* Каналы фазы.                                                              */
typedef enum
{
  NOTIFY_CH_LED1 = 0,   /* ALARM_LED_1 (PA11)                                */
  NOTIFY_CH_LED2,       /* ALARM_LED_2 (PA12)                                */
  NOTIFY_CH_VIBRO,      /* Vibro_On/Off (хук vector_macros, weak-заглушка)   */
  NOTIFY_CH_BEEP,       /* audio_beep(): 240 мс, внутренняя flash            */
  NOTIFY_CH_SOUND,      /* audio_play_now(sound_idx): клип образа (SND_*)    */
  NOTIFY_CH_COUNT
} notify_ch_t;

/* Одна фаза: канал + ВКЛ-время + пауза + повторы. Примеры:
     { NOTIFY_CH_VIBRO, 500, 200, 1, 0 }              - вибро 0.5 с, пауза;
     { NOTIFY_CH_BEEP,  NOTIFY_WAIT_END, 300, 3, 0 }  - 3 писка до конца
                                                        клипа, пауза 300 мс;
     { NOTIFY_CH_SOUND, NOTIFY_WAIT_END, 0, 1, SND_KOLOKOL_1 } - голос/звук
                                                        из образа, без паузы. */
typedef struct
{
  uint8_t  ch;          /* notify_ch_t                                       */
  uint16_t t_on_ms;     /* время включения; NOTIFY_WAIT_END - ждать плеер    */
  uint16_t t_off_ms;    /* пауза после выключения (0 = без паузы)            */
  uint8_t  repeats;     /* повторов фазы (0 и 1 = один раз)                  */
  uint16_t sound_idx;   /* для CH_SOUND: SND_* (audio_ids.h); прочим - 0     */
} notify_phase_t;

/* Последовательность фаз (сценарий оповещения). loop=1 - крутить вечно
   (тревога до подтверждения: Notify_Stop() или вытеснение).                 */
typedef struct
{
  const notify_phase_t *ph;    /* таблица фаз (const, во flash)              */
  uint8_t  n;                  /* число фаз (>=1)                            */
  uint8_t  prio;               /* NOTIFY_PRIO_*                              */
  uint8_t  loop;               /* 1 = повторять всю последовательность       */
} notify_seq_t;

/* ---------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------*/
void    Notify_Init(void);                  /* сброс движка (Menu_Init)      */
void    Notify_Start(const notify_seq_t *seq); /* запрос старта: любой поток;
                                                  старт в ближайший Tick
                                                  (~10 мс); prio решает,
                                                  вытеснит ли текущую         */
void    Notify_Stop(void);                  /* запрос стопа: любой поток;
                                                каналы погасятся в Tick      */
uint8_t Notify_Busy(void);                  /* 1 = что-то идёт/запрошено      */
void    Notify_Tick(void);                  /* движок: поток LVGL Task ~10 мс */

/* Готовые сценарии (таблицы в Notify_vector.c).                             */
extern const notify_seq_t NotifySeq_PowerOn;   /* включение: 3 писка         */

#ifdef __cplusplus
}
#endif

#endif /* NOTIFY_VECTOR_H */
