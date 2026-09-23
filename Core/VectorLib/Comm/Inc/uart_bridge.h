/**
  ******************************************************************************
  * @file    uart_bridge.h
  * @brief   Прозрачный мост UART4 <-> USART2 (по меткам в main.h: LoRa <-> LTE)
  *
  *          Всё, что пришло в один UART, немедленно уходит в другой без
  *          разбора протокола. Приём - прерывание побайтно в кольцевой буфер,
  *          передача - из потока бриджа блокирующим HAL_UART_Transmit.
  *
  *          Комфортный диапазон скоростей - до ~230400 включительно.
  *          Для 460800/921600 побайтовые прерывания уже дороги: туда нужен
  *          вариант на DMA (следующий этап), скажите, если потребуется.
  *
  *          ВАЖНО: пока мост включён, он единственный владелец huart2/huart4
  *          на приём. Если LTE/LoRa-драйвер начнёт сам читать свой UART -
  *          мост для этой линии нужно выключить (UART_BRIDGE_DIR_* ниже).
  ******************************************************************************
  */
#ifndef UART_BRIDGE_H
#define UART_BRIDGE_H

#include <stdint.h>
#include "vector_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Направления можно отключать независимо (0 = выключено) */
#define UART_BRIDGE_DIR_4_TO_2     1u      /* UART4 (LoRa) -> USART2 (LTE)   */
#define UART_BRIDGE_DIR_2_TO_4     1u      /* USART2 (LTE) -> UART4 (LoRa)   */

/* Вызывать один раз из tx_application_define(). */
void uart_bridge_init(void);

/* Отладочные счётчики (Expressions в отладчике)
   Типичная картина "мост не работает":
     rx4 растёт, tx2 не растёт  -> поток моста не просыпается или USART2 TX
                                   возвращает ошибку (ub_err, ub_dbg_err2)
     rx4 НЕ растёт              -> байты не доходят до UART4: линия/полудуплекс/
                                   скорость. После любой ошибки приём должен
                                   перезапускаться сам (ub_dbg_rearm4 растёт)
     ovf4 растёт                -> поток не успевает выгребать кольцо        */
extern volatile uint32_t ub_rx4_bytes;    /* принято из UART4                */
extern volatile uint32_t ub_rx2_bytes;    /* принято из USART2               */
extern volatile uint32_t ub_tx4_bytes;    /* отправлено в UART4              */
extern volatile uint32_t ub_tx2_bytes;    /* отправлено в USART2             */
extern volatile uint32_t ub_ovf4;         /* переполнения кольца UART4       */
extern volatile uint32_t ub_ovf2;         /* переполнения кольца USART2      */
extern volatile uint32_t ub_err;          /* ошибки HAL при передаче         */
extern volatile uint32_t ub_dbg_err4;     /* последний ErrorCode UART4       */
extern volatile uint32_t ub_dbg_err2;     /* последний ErrorCode USART2      */
extern volatile uint32_t ub_dbg_rearm4;   /* перезапусков приёма UART4       */
extern volatile uint32_t ub_dbg_rearm2;   /* перезапусков приёма USART2      */
extern volatile uint32_t ub_dbg_echo;     /* выброшено байт собственного эха
                                             (однопроводный полудуплекс)     */
extern volatile uint32_t ub_dbg_fill4;    /* заполнение кольца UART4         */
extern volatile uint32_t ub_dbg_fill2;    /* заполнение кольца USART2        */

#ifdef __cplusplus
}
#endif

#endif /* UART_BRIDGE_H */
