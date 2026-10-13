#ifndef AVIS_MENU_H
#define AVIS_MENU_H

#ifdef __cplusplus
extern "C" {
#endif
//=========================================================================================================================== 

//===========================================================================================================================
extern void 	Button_Menu(void);
extern void     DisplayIndication(void);                                // Индикация на дисплей
extern void 	DisplayChargeIndicationTurnOff(void);
//===========================================================================================================================
#if (CONFIG_TYPE_LCD == 0)
typedef enum {
  STATE_SCREEN_MEASURE = 0,	        	// Состояние вывода на экран концентрации
  STATE_SCREEN_MEASURE_TO_COUNTDOWN,    // Обратный отсчет на выключение
  STATE_SCREEN_COUNTDOWN_IMIT,    		// Обратный отсчет на выключение
  STATE_SCREEN_COUNTDOWN_SOS,    		// Обратный отсчет на SOS
  STATE_SCREEN_MEASURE_TO_MENU,         // Переход в меню, всплывающее сообщение
  STATE_SCREEN_INFO_START,              // Старт меню информации
  STATE_SCREEN_TRANSFER,                // Передача данных
  STATE_SCREEN_INFO_DATA,               // Время
  STATE_SCREEN_SETTING_INFO_DATA,       // Время установка
  STATE_SCREEN_INFO_SERIAL_NUM,         // Серийный номер
  STATE_SCREEN_INFO_F_W,                // Версия программы
  STATE_SCREEN_INFO_FORMULA,            // Молекулярная формула
  STATE_SCREEN_INFO_SERIAL_NUM_CHAN,	// Серийный номер сенсора
  STATE_SCREEN_INFO_LIMIT1,             // Состояние вывода на экран первого порога
  STATE_SCREEN_INFO_LIMIT2,             // Состояние вывода на экран второго порога
  STATE_SCREEN_INFO_LIMIT3,             // Состояние вывода на экран третьего порога
  STATE_SCREEN_INFO_SPAN,               // Состояние вывода на экран верхнего диапазона
  STATE_SCREEN_INFO_ERR,                // Ошибки прибора
  STATE_SCREEN_INFO_STEL,               // Состояние вывода на экран STEL
  STATE_SCREEN_INFO_TWA,                // Состояние вывода на экран TWA
  STATE_SCREEN_INFO_HIGH,               // Состояние вывода на экран HIGH
  STATE_SCREEN_INFO_RESET,              // Сброс значений STEL, TWA, HIGH
  STATE_SCREEN_INFO_DAY_CALIB,          // Количество дней до калибровки
  STATE_SCREEN_INFO_DAY_BT,             // Количество дней до BUMP TEST
  STATE_SCREEN_INFO_CLOSE,              // Выход из меню информации
  STATE_SCREEN_CALIB_ZERO,              // Состояние вывода на экран калибровка нуля
  STATE_SCREEN_CALIB_ZERO_START,        // Состояние вывода на экран калибровка нуля, start calib
  STATE_SCREEN_CALIB_ZERO_END,          // Конец режима калибровки нуля
  STATE_SCREEN_CALIB_SPAN,              // Состояние вывода на экран калибровка диапазона
  STATE_SCREEN_CALIB_SPAN_PASSWORD,     // Состояние вывода на экран калибровка диапазона, пароль
  STATE_SCREEN_CALIB_SPAN_ONE,          // Состояние вывода на экран калибровка диапазона, выбор меню с 1 сенсором
  STATE_SCREEN_CALIB_SPAN_SELECT_CHAN,  // Состояние вывода на экран калибровка диапазона, выбор канала калибровки, меню с 1 сенсором
  STATE_SCREEN_CALIB_SPAN_CONC,         // Состояние вывода на экран калибровка диапазона, концетрация, меню с 1 сенсором
  STATE_SCREEN_CALIB_SPAN_CONC_P,       // Состояние вывода на экран калибровка диапазона, изменение калибровочной концентрации, меню с 1 сенсором
  STATE_SCREEN_CALIB_SPAN_START,        // Состояние вывода на экран калибровка диапазона, start calib, меню с 1 сенсором
  STATE_SCREEN_CALIB_SPAN_END,          // Конец режима калибровки диапазона, меню с 1 сенсором
  STATE_SCREEN_CALIB_SPAN_MULTI,        // Состояние вывода на экран калибровка диапазона, выбор меню с 4 сенсорами
  STATE_SCREEN_CALIB_SPAN_CONC_MULTI,   // Состояние вывода на экран калибровка диапазона, концетрация, меню с 4 сенсорами
  STATE_SCREEN_CALIB_SPAN_START_MULTI,  // Состояние вывода на экран калибровка диапазона, start calib, меню с 4 сенсорами
  STATE_SCREEN_CALIB_SPAN_END_MULTI,    // Конец режима калибровки диапазона, меню с 1 сенсором
  STATE_SCREEN_CALIB_SPAN_EXIT,         // Состояние вывода на экран калибровка диапазона, выход из меню калибровки
  STATE_SCREEN_BUMP_TEST,               // Состояние вывода на экран BUMP TEST
  STATE_SCREEN_BUMP_TEST_START,         // Старт BUMP TEST
  STATE_SCREEN_BUMP_TEST_END,           // Конец BUMP TEST
  STATE_SCREEN_WARM,                    // Прогрев
  STATE_SCREEN_WARM_TO_MENU,
}STATE_SCREEN_AVIS;
#elif (CONFIG_TYPE_LCD == 1)
extern void 	Set_Start_Calib(uint16_t type_calib, uint8_t chan_one);
extern void 	Set_Save_Calib(void);
extern void 	Set_Exit_Calib(void);
extern uint16_t Get_Calib_Flag_State(void);
typedef enum {
  STATE_SCREEN_MEASURE = 0,	        	// Состояние вывода на экран концентрации
  STATE_SCREEN_MEASURE_TO_MENU,         // Переход в меню, всплывающее сообщение
  STATE_SCREEN_MEASURE_TO_COUNTDOWN,    // Обратный отсчет на выключение
  STATE_SCREEN_COUNTDOWN_IMIT,    		// Обратный отсчет на выключение
  STATE_SCREEN_COUNTDOWN_SOS,    		// Обратный отсчет на SOS
  STATE_SCREEN_PASSWORD,     			// Состояние вывода на экран, пароль

  STATE_SCREEN_MENU,              		// Меню прибора
  STATE_SCREEN_INFO_DEVICE,             // Информация прибора
  STATE_SCREEN_SETTING_DEVICE,			// Настройка прибора
  STATE_SCREEN_SERVICE_DEVICE,			// Сервис меню прибора

  STATE_SCREEN_SETTING_CHAN,       		// Настр. порог. значений
  STATE_SCREEN_SETTING_DATA,       		// Дата и время
  STATE_SCREEN_SETTING_RESET_LIMIT,   	// Сброс Stel, Twa, High
  STATE_SCREEN_SETTING_LANGUAGE,   		// Выбор языка
  STATE_SCREEN_SETTING_MEAS_CHAN, 		// Еденицы измерения

  STATE_SCREEN_CALIB_ZERO,              // Состояние вывода на экран калибровка нуля
  STATE_SCREEN_CALIB_SPAN,              // Состояние вывода на экран калибровка диапазона
  STATE_SCREEN_SERVICE_RESET_ERR,      	// Сброс Stel, Twa, High

  STATE_SCREEN_SETTING_CHAN_SET,       	// Настр. порог. значений
  STATE_SCREEN_SETTING_CHAN_SET_P,      // Настр. порог. значений ввод
  STATE_SCREEN_SETTING_DATA_SET,       	// Дата установить
  STATE_SCREEN_SETTING_TIME_SET,       	// Время устанвоить
  STATE_SCREEN_SETTING_MEAS_CHAN_SET,	// Еденицы измерения

  STATE_SCREEN_CALIB_ZERO_SELECT_CHAN,
  STATE_SCREEN_CALIB_ZERO_START,        // Состояние вывода на экран калибровка нуля, start calib
  STATE_SCREEN_CALIB_ZERO_END,          // Конец режима калибровки нуля
  STATE_SCREEN_CALIB_ZERO_START_MULTI,
  STATE_SCREEN_CALIB_ZERO_END_MULTI,    // Конец режима калибровки нуля
  STATE_SCREEN_CALIB_ZERO_END_MULTI_ACK,

  STATE_SCREEN_CALIB_SPAN_SELECT_CHAN,  // Состояние вывода на экран калибровка диапазона, выбор канала калибровки, меню с 1 сенсором
  STATE_SCREEN_CALIB_SPAN_CONC_P,       // Состояние вывода на экран калибровка диапазона, изменение калибровочной концентрации, меню с 1 сенсором
  STATE_SCREEN_CALIB_SPAN_START,        // Состояние вывода на экран калибровка диапазона, start calib, меню с 1 сенсором
  STATE_SCREEN_CALIB_SPAN_END,          // Конец режима калибровки диапазона, меню с 1 сенсором
  STATE_SCREEN_CALIB_SPAN_START_MULTI,  // Состояние вывода на экран калибровка диапазона, start calib, меню с 4 сенсорами
  STATE_SCREEN_CALIB_SPAN_END_MULTI,    // Конец режима калибровки диапазона, меню с 1 сенсором
  STATE_SCREEN_CALIB_SPAN_END_MULTI_ACK,

  STATE_SCREEN_BUMP_TEST,               // Состояние вывода на экран BUMP TEST
  STATE_SCREEN_BUMP_TEST_START,         // Старт BUMP TEST
  STATE_SCREEN_BUMP_TEST_END,           // Конец BUMP TEST

  STATE_SCREEN_WARM,                    // Прогрев
  STATE_SCREEN_WARM_AUTO_CALIB,         // Прогрев и автокалибровка
  STATE_SCREEN_WARM_INFO_DEVICE,		// Прогрев информация
}STATE_SCREEN_AVIS;
#elif (CONFIG_TYPE_LCD == 2)
extern void 	Button_1_Menu(void);
extern void 	Button_2_Menu(void);
extern void 	Button_3_Menu(void);
extern void 	Init_Start_Calib(uint16_t type_calib);
extern void 	Set_Start_Calib(uint8_t chan_one , uint8_t state);
extern void 	Set_Save_Calib(void);
extern void 	Set_Exit_Calib(void);
extern uint16_t Get_Calib_Flag_State(void);
typedef enum {
  STATE_SCREEN_MEASURE = 0,	        	// Состояние вывода на экран концентрации
  STATE_SCREEN_MEASURE_TO_MENU,         // Переход в меню, всплывающее сообщение
  STATE_SCREEN_MEASURE_TO_COUNTDOWN,    // Обратный отсчет на выключение
  STATE_SCREEN_COUNTDOWN_IMIT,    		// Обратный отсчет на выключение
  STATE_SCREEN_COUNTDOWN_SOS,    		// Обратный отсчет на SOS
  STATE_SCREEN_PASSWORD,     			// Состояние вывода на экран, пароль

  STATE_SCREEN_MENU,              		// Меню прибора
  STATE_SCREEN_INFO_DEVICE,             // Информация прибора
  STATE_SCREEN_SETTING_DEVICE,			// Настройка прибора
  STATE_SCREEN_SERVICE_DEVICE,			// Сервис меню прибора
  STATE_SCREEN_SERVICE_DEVICE_END,			// Сервис меню прибора вывод

  STATE_SCREEN_SETTING_CHAN,       		// Настр. порог. значений
  STATE_SCREEN_SETTING_DATA,       		// Дата и время
  STATE_SCREEN_SETTING_RESET_LIMIT,   	// Сброс Stel, Twa, High
  STATE_SCREEN_SETTING_LANGUAGE,   		// Выбор языка

  STATE_SCREEN_CALIB_ZERO,              // Состояние вывода на экран калибровка нуля
  STATE_SCREEN_CALIB_SPAN,              // Состояние вывода на экран калибровка диапазона
  STATE_SCREEN_SERVICE_RESET_ERR,      	// Сброс Stel, Twa, High

  STATE_SCREEN_SETTING_CHAN_SET,       	// Настр. порог. значений
  STATE_SCREEN_SETTING_CHAN_SET_P,      // Настр. порог. значений ввод
  STATE_SCREEN_SETTING_CHAN_SET_M_D,	// Натср. ед. изм. или дискретности
  STATE_SCREEN_SETTING_DATA_SET,       	// Дата установить
  STATE_SCREEN_SETTING_TIME_SET,       	// Время устанвоить
//  STATE_SCREEN_SETTING_MEAS_CHAN_SET,	// Еденицы измерения

  STATE_SCREEN_CALIB_ZERO_SELECT_CHAN,
  STATE_SCREEN_CALIB_ZERO_START,        // Состояние вывода на экран калибровка нуля, start calib
  STATE_SCREEN_CALIB_ZERO_END,          // Конец режима калибровки нуля
  STATE_SCREEN_CALIB_ZERO_SELECT_CHAN_MULTI,
  STATE_SCREEN_CALIB_ZERO_START_MULTI,
  STATE_SCREEN_CALIB_ZERO_END_MULTI,    // Конец режима калибровки нуля
  STATE_SCREEN_CALIB_ZERO_END_MULTI_ACK,

  STATE_SCREEN_CALIB_SPAN_SELECT_CHAN,  // Состояние вывода на экран калибровка диапазона, выбор канала калибровки, меню с 1 сенсором
  STATE_SCREEN_CALIB_SPAN_CONC_P,       // Состояние вывода на экран калибровка диапазона, изменение калибровочной концентрации, меню с 1 сенсором
  STATE_SCREEN_CALIB_SPAN_START,        // Состояние вывода на экран калибровка диапазона, start calib, меню с 1 сенсором
  STATE_SCREEN_CALIB_SPAN_END,          // Конец режима калибровки диапазона, меню с 1 сенсором
  STATE_SCREEN_CALIB_SPAN_SELECT_CHAN_MULTI,
  STATE_SCREEN_CALIB_SPAN_START_MULTI,  // Состояние вывода на экран калибровка диапазона, start calib, меню с 4 сенсорами
  STATE_SCREEN_CALIB_SPAN_END_MULTI,    // Конец режима калибровки диапазона, меню с 1 сенсором
  STATE_SCREEN_CALIB_SPAN_END_MULTI_ACK,

  STATE_SCREEN_BUMP_TEST,               // Состояние вывода на экран BUMP TEST
  STATE_SCREEN_BUMP_TEST_START,         // Старт BUMP TEST
  STATE_SCREEN_BUMP_TEST_END,           // Конец BUMP TEST

  STATE_SCREEN_WARM,                    // Прогрев
  STATE_SCREEN_WARM_AUTO_CALIB,         // Прогрев и автокалибровка
  STATE_SCREEN_WARM_INFO_DEVICE,		// Прогрев информация
//  STATE_SCREEN_FIRMWARE,
}STATE_SCREEN_AVIS;

#endif

extern STATE_SCREEN_AVIS state_screen;

typedef enum {
	STATE_BUTTON_CLR = 0,
	STATE_BUTTON_LONG,	           	// Состояние кнопки, долгое нажатие
	STATE_BUTTON_SHORT_LONG,		// Двойное нажатие короткое и длинное
	STATE_BUTTON_SHORT,		        // Состояние кнопки, короткое нажатие
	STATE_BUTTON_DOUBLE,			// Двойное короткое нажатие
	STATE_BUTTON_FAKE,             	// Состояние кнопки, неопределенное нажатие
}STATE_BUTTON_AVIS;

extern  STATE_BUTTON_AVIS state_button;
//===========================================================================================================================
#ifdef __cplusplus
}
#endif

#endif /* AVIS_MENU_H */
