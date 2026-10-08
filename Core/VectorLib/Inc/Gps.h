/**
  ******************************************************************************
  * @file    Gps.h
  * @brief   GPS/GNSS-модуль (LOCOSYS/Allystar): NMEA-приём, координаты для LoRa
  *
  *          Файл ПРИЛОЖЕН к проекту вместе с Gps.c (перенос с другого прибора,
  *          автор Dmitriy, 2024 Sep 2). Весь код закрыт `#if CONFIG_GPS`, а
  *          CONFIG_GPS в config_device.h пока = 0, поэтому в сборку модуль не
  *          попадает и ничего не ломает - это база под включение.
  *
  *          ПОРЯДОК ВКЛЮЧЕНИЯ (что нужно довести до конца при CONFIG_GPS 1):
  *            1. Vector_main.h: раскомментировать `#include "shared_types.h"`
  *               - нужен тип SNS_CFG (Gps_Init/Gps_Init_Nav_Sys).
  *            2. Перенести сюда из вашего Gps.h типы и константы модуля:
  *               GNSS_CHIP_TYPE (CHIP_ALLYSTAR_OLD, CHIP_LOCOSYS_AIROHA_NEW,
  *               ...), DETECT_STATE_t (DETECT_STATE_START_115200, ...),
  *               команды CMD_EN_DIS_MSG / CMD_TYPE_START / ...,
  *               типы сообщений TYPE_MSG_GGA/GLL/GSA/GSV/RMC/VTG/ZDA/GRS/TXT,
  *               TYPE_HOT_START / TYPE_WARM_START / TYPE_COLD_START / TYPE_RESET,
  *               навигационные системы TYPE_NAV_GPS_L1 / BEIDOU_B1 /
  *               GLONASS_G1 / GALILEO_E1.
  *            3. Определить USART_GPS (порт модуля) и включить его IRQ в кубе -
  *               см. PLAN.md раздел 2 (UART пока намеренно не настраиваем).
  *               В Gps.c есть AT32-вызов `usart_init(USART_GPS, baud, ...)`
  *               (Uart_Gps_Set_Baudrate) - на STM32/HAL заменить на
  *               HAL_UART_Init или перенастройку BRR.
  *            4. Общие хелперы, которых в проекте пока нет (нужны и LoRa, и
  *               GPS): Delay(ms), GetTick(), Search_text(). По правилам
  *               проекта время - только тик RTOS (vector_tick.h): GetTick()
  *               -> VTICK_MS(), Delay() -> VTICK_SLEEP_MS().
  *            5. Приём байтов: в Gps.c его кладёт Gps_Data_Verification(data),
  *               а вызывается она из ISR приёма UART; таймаут кадра считает
  *               Uart_Gps_Receive_Timer_Inc() - нужен вызов с периодом 1 мс
  *               (в проекте для этого есть TIM3 1 кГц, PLAN.md раздел 5 этап 4).
  *               Альтернатива без ISR-логики: кольцо InputBuffer[TYPE_GPS]
  *               читает Gps_Receive() в потоке Receiver Task (тип TYPE_GPS в
  *               buffer.h уже добавлен).
  *
  *          КУДА ПОДКЛЮЧЕНО В ПРОЕКТЕ:
  *            Gps_Receive() - поток "Receiver Task" (Vector_main.c),
  *            Gps_Run()     - поток "Measure Task" из Vector_Run_Measure().
  ******************************************************************************
  */
#ifndef GPS_H
#define GPS_H

#include <stdint.h>
#include "config_device.h"      /* CONFIG_GPS */

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_GPS

#include <math.h>               /* double_t */
#include "shared_types.h"       /* SNS_CFG */

#define USART_GPS		(husart1)
#define TIME_OUT_GPS	((uint32_t)((0.05)/TIME_DEL_1 + 0.5))
//===========================================================================================================================
//Перечисление типов поддерживаемых чипсетов
typedef enum {
	CHIP_ALLYSTAR_OLD = 0,
	CHIP_LOCOSYS_AIROHA_NEW = 1
} GNSS_CHIP_TYPE;

extern GNSS_CHIP_TYPE Gps_Chip_Type; // Глобальный флаг активного чипа

// Перечисление внутренних шагов автомата состояний детекта чипа
typedef enum {
    DETECT_STATE_START_115200 = 0, // Включение 115200 и сброс буфера
    DETECT_STATE_LISTEN_115200,    // Ожидание первого пакета '$' от LOCOSYS
} DETECT_STATE_t;
//===========================================================================================================================
typedef enum{
	CMD_EN_DIS_MSG = 0,
	CMD_CONF_NAV_SYS,
	CMD_TYPE_START,
	CMD_SAVE_CONFIG,
	CMD_BLOCK_PROPRIETARY,
}COMMAND_GPS;

typedef enum{
	TYPE_NAV_GPS_L1 = 0,
	TYPE_NAV_GLONASS_G1,
	TYPE_NAV_BEIDOU_B1,
	TYPE_NAV_NOP,
	TYPE_NAV_GALILEO_E1,
	TYPE_NAV_QZSS_L1,
	TYPE_NAV_SBAS_L1,
	TYPE_NAV_IRNSS_L5,
	TYPE_NAV_GPS_L2C,
	TYPE_NAV_GPS_L5,
	TYPE_NAV_GLONASS_G2,
	TYPE_NAV_BEIDOU_B1C,
	TYPE_NAV_BEIDOU_B2,
	TYPE_NAV_BEIDOU_B2A,
	TYPE_NAV_BEIDOU_B3I,
	TYPE_NAV_BEIDOU_B5,
	TYPE_NAV_GALILEO_E5A,
	TYPE_NAV_QZSS_L2C,
	TYPE_NAV_QZSS_L5,
}TYPE_NAV;

typedef enum{
	TYPE_MSG_GGA = 0,
	TYPE_MSG_GLL,
	TYPE_MSG_GSA,
	TYPE_MSG_GRS,
	TYPE_MSG_GSV,
	TYPE_MSG_RMC,
	TYPE_MSG_VTG,
	TYPE_MSG_ZDA,
	TYPE_MSG_TXT = 0x20,
}TYPE_MSG_GPS;

typedef enum{
	TYPE_RESET = 0,
	TYPE_COLD_START,
	TYPE_WARM_START,
	TYPE_HOT_START,
}TYPE_MSG_START;

/* ---------------------------------------------------------------------------                                     */
typedef struct
{
    uint8_t  receive[256];      /* накопитель принятых байтов               */
    uint8_t  transmit[400];     /* буфер передачи (команды модулю)          */
    uint8_t  count_byter_r;     /* сколько байтов в receive                 */
    uint8_t  TimeRX;            /* таймаут межбайтового интервала (1 мс тики)*/
    uint8_t  TimeFlagRX;        /* 1 = кадр принят, можно разбирать         */
    uint32_t TimerReceive;      /* метка времени последнего кадра           */
} Message_buffer_uart_gps;

extern Message_buffer_uart_gps buffer_uart_gps;
//===========================================================================================================================
extern double_t Time_coord;
extern double_t Latitude;
extern double_t Longitude;
//===========================================================================================================================
/* Результат разбора NMEA: координаты в градусах (double) и масштабированные
   в 1e-7 градуса (int32) - именно их забирает LoRa-трек
   (Lora_UpdateGPSTrack).                                                    */
extern GNSS_CHIP_TYPE Gps_Chip_Type;    /* какой чип определился при старте  */
extern double_t  Time_coord;
extern double_t  Latitude;
extern double_t  Longitude;
extern int32_t   g_lat_scaled;
extern int32_t   g_lon_scaled;
extern uint32_t  Gps_type_nav_pre;      /* маска навигационных систем        */

/* --- инициализация и состояние модуля ------------------------------------ */
uint8_t Gps_Init(SNS_CFG *Cfg_struct);          /* 1 = модуль инициализирован */
void    Gps_Init_Nav_Sys(SNS_CFG *Cfg_struct);  /* выбор систем GPS/GLONASS/BDS/GAL */
void    Gps_DeInit(uint8_t init);
uint8_t get_state_init_flag_gps(void);          /* 1 = инициализирован        */
uint8_t get_state_err_gps(void);                /* 1 = нет данных > 20 с      */
uint8_t Gps_Auto_Detect_Chip(void);             /* определение чипа по ответам*/
void    Uart_Gps_Set_Baudrate(uint32_t baud);

/* --- обмен ---------------------------------------------------------------- */
void    Gps_Transmit(uint8_t command, uint16_t param1, uint16_t param2);
void    Gps_Receive(void);              /* разбор принятого кадра (NMEA GLL) */

/* --- приём байтов из ISR и таймаут кадра ---------------------------------- */
void    Gps_Data_Verification(uint8_t data);   /* звать из ISR приёма UART   */
void    Uart_Gps_Receive_Timer_Inc(void);      /* звать с периодом 1 мс      */
void    Gps_Receive_flag_set(uint8_t flag);
uint8_t Gps_Receive_flag_read(void);
uint8_t Gps_Receive_flag_navigation(void);     /* 1 = есть валидные координаты */

/* --- разбор NMEA-строк ---------------------------------------------------- */
void    parsing_gga(uint8_t *data);
void    parsing_gll(uint8_t *data);
void    parsing_rmc(uint8_t *data);

#endif /* CONFIG_GPS */

#ifdef __cplusplus
}
#endif

#endif /* GPS_H */
