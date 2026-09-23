/**
  ******************************************************************************
  * @file    uart_bridge.h
  * @brief   Прозрачный мост UART4 <-> USART2 (LoRa <-> LTE)
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

#ifdef __cplusplus
extern "C" {
#endif

/* Направления можно отключать независимо (0 = выключено) */
#define UART_BRIDGE_DIR_4_TO_2     1u      /* UART4 (LoRa) -> USART2 (LTE)   */
#define UART_BRIDGE_DIR_2_TO_4     1u      /* USART2 (LTE) -> UART4 (LoRa)   */

/* Вызывать один раз из tx_application_define(). */
void uart_bridge_init(void);

/* Отладочные счётчики */
extern volatile uint32_t ub_rx4_bytes;    /* принято из UART4                */
extern volatile uint32_t ub_rx2_bytes;    /* принято из USART2               */
extern volatile uint32_t ub_tx4_bytes;    /* отправлено в UART4              */
extern volatile uint32_t ub_tx2_bytes;    /* отправлено в USART2             */
extern volatile uint32_t ub_ovf4;         /* переполнения кольца UART4       */
extern volatile uint32_t ub_ovf2;         /* переполнения кольца USART2      */
extern volatile uint32_t ub_err;          /* ошибки HAL при передаче         */

#ifdef __cplusplus
}
#endif

#endif /* UART_BRIDGE_H */
