#ifndef SHARED_MACROS_H
#define SHARED_MACROS_H
//--------------------------------------------------------------------------------------------------------------
//Маски для ошибок в байте состояния----------------------------------------------------------------------------
#define ERROR_MASK_ST_1                 (0x03FF)
#define ERROR_MASK_ST_1_1       		(0x007F)
#define ERROR_COUNT_ST_1                (0x0A)
#define ERROR_BIT_ST_1                  (0x0001)

#define ERROR_MASK_ST_2               	(0x03FF)
#define ERROR_MASK_ST_2_1             	(0x03F8)
#define ERROR_COUNT_ST_2              	(0x0A)

#define ERROR_BIT_ST_2                  (0x0001)

//--------------------------------------------------------------------------------------------------------------
//===========================================================================================================================
#define SETBIT(var,bit)		        (var |= (1 << bit))
#define CLRBIT(var,bit)		        (var &= ~(1 << bit))
#define TESTBIT(var,bit)	        (var & (1 << bit))
//--------------------------------------------------------------------------------------------------------------
#define TO_GO_STATE_SCREEN(x)	(state_screen = x)
#define IS_STATE_SCREEN()		(state_screen)
#define SET_STATE_BUTTON(x)		(state_button = x)
#define TEST_STATE_BUTTON(x)	(state_button == x)

#define SET_STATE_BUTTON1(x)	(button.state_button = x)
#define TEST_STATE_BUTTON1(x)	(button.state_button == x)
#define SET_STATE_BUTTON2(x)	(button2.state_button = x)
#define TEST_STATE_BUTTON2(x)	(button2.state_button == x)
#define SET_STATE_BUTTON3(x)	(button3.state_button = x)
#define TEST_STATE_BUTTON3(x)	(button3.state_button == x)
//--------------------------------------------------------------------------------------------------------------
#define LOBYTE(w)			(((uint8_t*)(&w))[0])
#define HIBYTE(w)			(((uint8_t*)(&w))[1])

#define LOWORD(w)			(((uint16_t*)(&w))[0])
#define HIWORD(w)			(((uint16_t*)(&w))[1])

#define to_word(w)			((((uint8_t*)(&w))[1] << 8) + ((uint8_t*)(&w))[0])
//--------------------------------------------------------------------------------------------------------------

#endif /* SHARED_MACROS_H */
