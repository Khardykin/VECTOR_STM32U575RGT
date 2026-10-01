#ifndef BUFFER_H_
#define BUFFER_H_

#define DMA_USART 0
#define BUFFER_LENGTH 255
enum TYPE_USART
{
 TYPE_USART = 0,
 TYPE_DEBUG	,
 TYPE_RF	,
 TYPE_BLE	,
 TYPE_SENSOR,
 TYPE_USART_COUNT,
};

#define TYPE_LORA (TYPE_RF)
//======================================================================================================================================
typedef struct {
	uint8_t buffer[BUFFER_LENGTH]; //Буфер
	uint16_t begin;    //Метки:начало буфера
    uint16_t end;      //Метки:конец буфера
} T_Buffer;

extern T_Buffer InputBuffer[TYPE_USART_COUNT]; //Буфер приема по usart

//======================================================================================================================================
extern void Init_Buffer(T_Buffer *Buffer);                       //Инициализация меток буфера

extern uint16_t add_to_buffer(T_Buffer *Buffer, uint8_t ch);    //добавление принятого байта

extern uint8_t check_buffer(T_Buffer *Buffer);                  //проверка на наличие необработаного байта в буфере

extern uint16_t receive_buffer(T_Buffer *Buffer, uint8_t * ch); //чтение пакета

extern void transmit_buffer(uint8_t *pData, uint16_t Size, uint8_t type_transmit);     //передача пакета


#endif /* BUFFER_H_ */
