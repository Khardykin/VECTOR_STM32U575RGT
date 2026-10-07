#ifndef BUFFER_H_
#define BUFFER_H_

#include <stdint.h>

#define DMA_USART 0
#define BUFFER_LENGTH 255
enum TYPE_USART
{
 TYPE_USART = 0,
 TYPE_DEBUG	,
 TYPE_RF	,
 TYPE_BLE	,
 TYPE_LTE	,	//LTE-модем: кольцо приёма для потока Receiver Task (Vector_main.c)
 TYPE_GPS	,	//GPS/GNSS-модуль: кольцо приёма и тип передачи (Gps.c)
 TYPE_SENSOR,
 TYPE_USART_COUNT,
};

//LoRa и LTE - разные модули одного потока приёма: у каждого своё кольцо
#define TYPE_LORA (TYPE_RF)
//======================================================================================================================================
/* Кольцевой буфер "один производитель - один потребитель" (SPSC):
 *   производитель - ISR приёма UART (add_to_buffer), ВЛАДЕЕТ меткой end;
 *   потребитель   - поток (check_buffer/receive_buffer), ВЛАДЕЕТ меткой begin.
 * Метки volatile: их пишут в прерывании, а читают в потоке - без volatile
 * компилятор вправе закэшировать значение в регистре и не увидеть обновление.
 *
 * Метки всегда в диапазоне 0..BUFFER_LENGTH-1 и заворачиваются сами, поэтому
 * обнулять их при опустошении НЕ НУЖНО (раньше потребитель обнулял ещё и
 * чужую метку end - это теряло байт, принятый в тот же момент из ISR).
 *
 * Полезная вместимость = BUFFER_LENGTH-1 = 254 байта: один слот всегда пуст,
 * иначе "пусто" (begin == end) не отличить от "полно".                       */
typedef struct {
	uint8_t buffer[BUFFER_LENGTH]; //Буфер
	volatile uint16_t begin;    //Метки:начало буфера (пишет только потребитель)
	volatile uint16_t end;      //Метки:конец буфера (пишет только ISR приёма)
} T_Buffer;

extern T_Buffer InputBuffer[TYPE_USART_COUNT]; //Буфер приема по usart

//======================================================================================================================================
extern void Init_Buffer(T_Buffer *Buffer);                       //Инициализация меток буфера

extern uint16_t add_to_buffer(T_Buffer *Buffer, uint8_t ch);    //добавление принятого байта (контекст ISR; 1 = буфер полон, байт потерян)

extern uint8_t check_buffer(T_Buffer *Buffer);                  //проверка на наличие необработаного байта в буфере

extern uint16_t receive_buffer(T_Buffer *Buffer, uint8_t * ch); //чтение пакета

extern void transmit_buffer(uint8_t *pData, uint16_t Size, uint8_t type_transmit);     //передача пакета


#endif /* BUFFER_H_ */
