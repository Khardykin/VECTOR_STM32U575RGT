/*
 * gps.c
 *
 *  Created on: 2024 Sep 2
 *      Author: Dmitriy
 */
#include "Vector_main.h"   /* агрегатор проекта: config_device.h (CONFIG_GPS),
                              buffer.h (TYPE_GPS, transmit_buffer), позже
                              shared_types.h (SNS_CFG) - см. PLAN.md п.25-26  */
#include "Gps.h"
#if CONFIG_GPS

#include <string.h>   /* strstr/memset/strlen: в Avis их тянул Avis_main.h */
#include <stdio.h>    /* sprintf (ветка TEST_GPS)                          */

static uint16_t Gps_Crc(uint8_t *data, uint16_t count);
static double_t NmeaToDecimalDegrees(double_t nmea_coord);
void Uart_Gps_Set_Baudrate(uint32_t baud);
uint8_t Gps_Auto_Detect_Chip(void);
//===========================================================================================================================
static DETECT_STATE_t Gps_Detect_State = DETECT_STATE_START_115200;
static uint32_t 		Gps_Detect_Timeout_Timer = 0;
GNSS_CHIP_TYPE			Gps_Chip_Type = CHIP_ALLYSTAR_OLD;
static uint8_t 			Gps_Verification_Mode = 0;
static uint8_t 			Gps_flag_err = 0;
static uint8_t 			Gps_Init_flag = 0;
static uint8_t			Gps_flag_receive = 0;
static uint8_t			Gps_count_receive = 0;
static uint8_t			Gps_flag_navigation = 0;
//===========================================================================================================================
Message_buffer_uart_gps buffer_uart_gps = {0};
double_t Time_coord = 0;
double_t Latitude = 0;
double_t Longitude = 0;
int32_t g_lat_scaled = 0;
int32_t g_lon_scaled = 0;
uint32_t Gps_type_nav_pre = 0;
//===========================================================================================================================
static uint16_t Gps_Crc(uint8_t *data, uint16_t count)
{
	uint16_t sum1 = 0;
	uint16_t sum2 = 0;

   for(uint16_t index = 0; index < count; index++)
   {
      sum1 = (sum1 + data[index]);
      sum2 = (sum2 + sum1);
   }
   sum1 = sum1&0xff;
   sum2 = sum2&0xff;

   data[count] = sum1;
   data[count+1] = sum2;

   return (sum2 << 8) | sum1;
}

//===========================================================================================================================
void Gps_Receive_flag_set(uint8_t flag)
{
	Gps_flag_receive = flag;
}

//===========================================================================================================================
uint8_t Gps_Receive_flag_read(void)
{
	return Gps_flag_receive;
}

//===========================================================================================================================
uint8_t Gps_Receive_flag_navigation(void)
{
	return Gps_flag_navigation;
}

//===========================================================================================================================
void Gps_Data_Verification(uint8_t data)
{
	uint16_t len = 0;

	if((Gps_Verification_Mode == 0) && (get_state_init_flag_gps() == 0)){
		return;
	}

	if(data == '$'){
		if((Gps_Receive_flag_read() == 0)){
			Gps_count_receive = 0;
			Gps_Receive_flag_set(1);
		}
	}
	else if((Gps_Verification_Mode == 0) && (Gps_Receive_flag_read() == 1) && (Gps_count_receive >= 6)){
		if(strstr(buffer_uart_gps.receive, "GLL") != NULL){
			buffer_uart_gps.TimeRX = TIME_OUT_GPS;
		}
		else{
			Gps_Receive_flag_set(0);
			len = buffer_uart_gps.count_byter_r;
			buffer_uart_gps.count_byter_r = 0;
			memset(&buffer_uart_gps.receive[0], 0, len);
		}
	}

	if(Gps_Receive_flag_read() == 1){
		Gps_count_receive ++;
		if(buffer_uart_gps.count_byter_r < 255){
			buffer_uart_gps.receive[buffer_uart_gps.count_byter_r] = data;
			buffer_uart_gps.count_byter_r++;
		}
	}
}

//===========================================================================================================================
void Uart_Gps_Receive_Timer_Inc(void)
{
	if(buffer_uart_gps.TimeRX){
		buffer_uart_gps.TimeRX --;
		if(buffer_uart_gps.TimeRX == 0){
			buffer_uart_gps.TimeFlagRX  = 1;
		}
	}
}

//===========================================================================================================================
uint8_t get_state_err_gps(void)
{
	return Gps_flag_err;
}

//===========================================================================================================================
uint8_t get_state_init_flag_gps(void)
{
	return Gps_Init_flag;
}

//===========================================================================================================================
void Gps_DeInit(uint8_t init)
{
	Gps_Init_flag = 0;
	Gps_flag_err = 0;
}

//===========================================================================================================================
void Uart_Gps_Set_Baudrate(uint32_t baud)
{
	usart_init(USART_GPS, baud, USART_DATA_8BITS, USART_STOP_1_BIT);
}

//===========================================================================================================================
// Возвращает: 0 - в процессе детекта, 1 - LOCOSYS (115200) определен, 2 - Allystar (9600) определен
uint8_t Gps_Auto_Detect_Chip(void)
{
    uint16_t len = 0;

    switch (Gps_Detect_State)
    {
		case DETECT_STATE_START_115200:
			Gps_Verification_Mode = 1;
			Gps_Receive_flag_set(0);
			buffer_uart_gps.count_byter_r = 0;
			memset(&buffer_uart_gps.receive, 0, 256);

			Uart_Gps_Set_Baudrate(115200);

			// ИСПРАВЛЕНО: Пингуем официальной командой запроса версии прошивки LSSCFGPTVER
			uint8_t ping_lsscfg[] = "$LSSCFGPTVER\r\n";
			transmit_buffer(ping_lsscfg, sizeof(ping_lsscfg) - 1, TYPE_GPS);

			Gps_Detect_Timeout_Timer = GetTick();
			Gps_Detect_State = DETECT_STATE_LISTEN_115200;
			break;

        case DETECT_STATE_LISTEN_115200:
            // ИСПРАВЛЕНО: Никаких флагов TimeFlagRX!
            // Как только в прерывании накопилось хотя бы 3-5 байт, сразу проверяем первый символ
            if (buffer_uart_gps.count_byter_r >= 3)
            {
                // Если поток на 115200 bps начался с '$' — это гарантированно LOCOSYS!
                if (buffer_uart_gps.receive[0] == '$')
                {
                    Gps_Chip_Type = CHIP_LOCOSYS_AIROHA_NEW;
                    Gps_Detect_State = DETECT_STATE_START_115200; // Сброс автомата на будущее
                    Gps_Verification_Mode = 0;                    // Выключаем верификацию
                    return 1; // Успешно определили LOCOSYS
                }

                // Если прилетел не '$' (какой-то мусор от помех) — очищаем буфер и слушаем дальше, пока идет таймаут
                len = buffer_uart_gps.count_byter_r;
                buffer_uart_gps.count_byter_r = 0;
                memset(&buffer_uart_gps.receive, 0, len);
                Gps_Receive_flag_set(0);
            }

            // ТАЙМАУТ: Если за 5 секунд на скорости 115200 символ '$' так и не появился в начале буфера,
            // значит LOCOSYS на плате нет — это старая микросхема Allystar!
            if ((GetTick() - Gps_Detect_Timeout_Timer) > 5000)
            {
                // Жестко переключаемся на старый чип и скорость 9600 bps
                Gps_Chip_Type = CHIP_ALLYSTAR_OLD;
                Uart_Gps_Set_Baudrate(9600);

                Gps_Detect_State = DETECT_STATE_START_115200; // Сброс автомата
                Gps_Verification_Mode = 0;                    // Выключаем верификацию

                return 2; // Мгновенно выходим в Allystar без ожидания ответов
            }
            break;

        default:
            Gps_Detect_State = DETECT_STATE_START_115200;
            break;
    }

    return 0; // Детект еще идет на 115200, продолжаем опрос в Gps_Run
}

//===========================================================================================================================
uint8_t Gps_Init(SNS_CFG *Cfg_struct)
{
	uint8_t message_rate = 10;
	uint32_t Gps_type_nav = Cfg_struct->Config_common.Gps_type_nav & 0x7FFFF;
	uint32_t Gps_type_start = (Cfg_struct->Config_common.Gps_type_nav >> 24) & 0x03;

	uint8_t detect_status = Gps_Auto_Detect_Chip();
	// Если вернулся 0 — чип еще не определен, выходим и ждем следующего прохода в Gps_Run
	if (detect_status == 0){
		return 0;
	}
//    Uart_Gps_Set_Baudrate(115200);
//    Delay(20);
//	Gps_Chip_Type = CHIP_LOCOSYS_AIROHA_NEW;

	if (Gps_Chip_Type == CHIP_LOCOSYS_AIROHA_NEW)
	{
		// 1. Поштучно гасим лишние NMEA сообщения
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_GGA, 0); Delay(25);
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_GSA, 0); Delay(25);
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_GSV, 0); Delay(25);
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_RMC, 0); Delay(25);
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_VTG, 0); Delay(25);
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_ZDA, 0); Delay(25);

		// 2. Настраиваем аппаратную выдачу GLL раз в 10 секунд (message_rate)
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_GLL, message_rate); Delay(25);

		// 3. Отключаем отладочный мусор $LSJAM и $LSSPF
		Gps_Transmit(CMD_BLOCK_PROPRIETARY, 0, 0); Delay(25);

		// 4. Конфигурируем маску созвездий спутников
		Gps_Transmit(CMD_CONF_NAV_SYS, (Gps_type_nav) >> 16, (Gps_type_nav)); Delay(25);

		// 5. Записываем примененную конфигурацию во Flash-память модуля LOCOSYS
		Gps_Transmit(CMD_SAVE_CONFIG, 0, 0); Delay(40); // Чуть увеличим паузу для завершения записи во Flash

		// 6. ДОБАВЛЕНО: Выполняем перезапуск модуля в нужном режиме (Hot/Cold/Reset)
		// Передаем Gps_type_start, который был извлечен из структуры конфигурации
		Gps_Transmit(CMD_TYPE_START, Gps_type_start, 0); Delay(25);

		Gps_type_nav_pre = Gps_type_nav;
	}
	else
	{
		// Инициализируем бинарный протокол Allystar на скорости 9600
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_GGA, 0);
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_GLL, message_rate);
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_GSA, 0);
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_GRS, 0);
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_GSV, 0);
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_RMC, 0);
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_VTG, 0);
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_ZDA, 0);
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_TXT, 0);

		Gps_Transmit(CMD_SAVE_CONFIG, 0, 0);

		Gps_Transmit(CMD_CONF_NAV_SYS, (Gps_type_nav) >> 16, (Gps_type_nav));
		Gps_type_nav_pre = Gps_type_nav;
		Gps_Transmit(CMD_TYPE_START, Gps_type_start, 0);
	}

	Gps_Init_flag = 1;
	Gps_flag_err = 0;

	return 1;
}


//===========================================================================================================================
void Gps_Init_Nav_Sys(SNS_CFG *Cfg_struct)
{
	uint32_t Gps_type_nav = Cfg_struct->Config_common.Gps_type_nav&0x7FFFF;
	uint32_t Gps_type_start = (Cfg_struct->Config_common.Gps_type_nav>>24)&0x03;
	if(Gps_type_nav_pre != Gps_type_nav){
		if (Gps_Chip_Type == CHIP_LOCOSYS_AIROHA_NEW)
		{
			// --- ДОБАВЛЕНО: Динамическое переключение спутников для LOCOSYS ---
			// 1. Отправляем новую маску созвездий ($LSSCFGSYS)
			Gps_Transmit(CMD_CONF_NAV_SYS, (Gps_type_nav) >> 16, (Gps_type_nav));
			Delay(25);

			// 2. Сохраняем измененные настройки во Flash-память ($LSSCFGSAVE)
			Gps_Transmit(CMD_SAVE_CONFIG, 0, 0);
			Delay(40);

			// 3. Выполняем перезапуск модуля ($LSSCFGRESET), чтобы настройки вступили в силу
			Gps_Transmit(CMD_TYPE_START, Gps_type_start, 0);
			Delay(25);

			// Фиксируем новую маску созвездий
			Gps_type_nav_pre = Gps_type_nav;
		}
		else
		{
			// Для старого чипа Allystar
			Gps_Transmit(CMD_CONF_NAV_SYS, (Gps_type_nav)>>16, (Gps_type_nav));
			Gps_type_nav_pre = Gps_type_nav;
			Gps_Transmit(CMD_TYPE_START, Gps_type_start, 0);
			Gps_Transmit(CMD_SAVE_CONFIG, 0, 0);
		}
	}
}

//===========================================================================================================================
void Gps_Transmit(uint8_t command, uint16_t param1, uint16_t param2)
{
    uint8_t *data_command = buffer_uart_gps.transmit;
    uint16_t len = 0;

    // ====================================================================================
    // --- ПРОТОКОЛ LOCOSYS LV-1010-L1 (AIROHA ЧИПСЕТ) ---
    // ====================================================================================
    if (Gps_Chip_Type == CHIP_LOCOSYS_AIROHA_NEW)
	{
		char temp_str[80];
		memset(temp_str, 0, sizeof(temp_str));

		switch(command)
		{
			case CMD_EN_DIS_MSG:
				{
					uint16_t lscfg_msg_id = 0;
					uint8_t lscfg_action = (param2 > 0) ? 1 : 0;

					// Маппинг вашего перечисления TYPE_MSG_GPS на msgID по документу LOCOSYS (Стр. 5)
					switch(param1) {
						case TYPE_MSG_GGA: lscfg_msg_id = 0;  break;
						case TYPE_MSG_GSA: lscfg_msg_id = 1;  break;
						case TYPE_MSG_GSV: lscfg_msg_id = 2;  break;
						case TYPE_MSG_VTG: lscfg_msg_id = 3;  break;
						case TYPE_MSG_RMC: lscfg_msg_id = 5;  break;
						case TYPE_MSG_GLL: lscfg_msg_id = 13; break;
						case TYPE_MSG_ZDA: lscfg_msg_id = 20; break;
						default: return; // Остальные типы не поддерживаются модулем напрямую
					}

					// Форматируем по документу: $LSSCFGMSG,msgClass(0),msgID,msgCtrl
					// Пример отключения GGA: $LSSCFGMSG,0,0,0\r\n
					// Пример включения GLL:  $LSSCFGMSG,0,13,1\r\n
					sprintf(temp_str, "LSSCFGMSG,0,%d,%d", lscfg_msg_id, lscfg_action);
				}
				break;

            case CMD_CONF_NAV_SYS:
                {
                    // Теперь здесь СТРОГО чистая настройка созвездий
                    uint32_t nav_mask = ((uint32_t)param1 << 16) | param2;
                    uint32_t sysMask = 0;

                    if (nav_mask & (1 << TYPE_NAV_GPS_L1))      sysMask |= (1 << 0);
                    if (nav_mask & (1 << TYPE_NAV_BEIDOU_B1))   sysMask |= (1 << 2);
                    if (nav_mask & (1 << TYPE_NAV_GLONASS_G1))  sysMask |= (1 << 6);
                    if (nav_mask & (1 << TYPE_NAV_GALILEO_E1))  sysMask |= (1 << 7);

                    if (sysMask == 0) {
                        sysMask = 0xC5;
                    }
                    sprintf(temp_str, "LSSCFGSYS,%lu", sysMask);
                }
                break;

            case CMD_BLOCK_PROPRIETARY:
                {
                    // Выключаем служебное сообщение JAM (msgID = 10) через msgClass = 0
                    sprintf(temp_str, "LSSCFGMSG,0,10,0");

                    // Чтобы за один вызов выключить и SPF (msgID = 39), мы отправим его прямо отсюда в UART,
                    // а строка JAM автоматически соберется и отправится общим кодом ниже!
                    uint8_t *cmd_buf = buffer_uart_gps.transmit;
                    char spf_str[40] = {0};
                    sprintf(spf_str, "LSSCFGMSG,0,39,0");
                    uint16_t spf_len = sprintf((char*)cmd_buf, "$%s\r\n", spf_str);
                    transmit_buffer(cmd_buf, spf_len, TYPE_GPS);
                    Delay(25);
                }
                break;

			case CMD_SAVE_CONFIG:
				{
					// Форматируем по документу: $LSSCFGSAVE
					sprintf(temp_str, "LSSCFGSAVE");
				}
				break;

            case CMD_TYPE_START:
                {
                    uint8_t lscfg_reset_type = 1; // По умолчанию Cold start (1)

                    // Маппинг вашего перечисления TYPE_MSG_START на параметры LOCOSYS (Стр. 3)
                    switch(param1) {
                        case TYPE_HOT_START:
                            lscfg_reset_type = 0; // Hot start
                            break;
                        case TYPE_WARM_START:
                            lscfg_reset_type = 0; // В чипах LOCOSYS Warm старт активируется через тип 0 (Hot) без затирки SRAM
                            break;
                        case TYPE_COLD_START:
                            lscfg_reset_type = 1; // Cold start
                            break;
                        case TYPE_RESET:
                            lscfg_reset_type = 2; // Factory reset (Заводской сброс)
                            break;
                        default:
                            lscfg_reset_type = 1;
                            break;
                    }

                    // Форматируем по документу: $LSSCFGRESET,Type
                    sprintf(temp_str, "LSSCFGRESET,%d", lscfg_reset_type);
                }
                break;

			default:
				return;
		}

		if (strlen(temp_str) == 0) {
			return;
		}

		// Собираем итоговую строку БЕЗ контрольной суммы, строго со знаком '$' на старте и "\r\n" в конце
		len = sprintf((char*)data_command, "$%s\r\n", temp_str);

		transmit_buffer(data_command, len, TYPE_GPS);
		memset(data_command, 0, len + 4);
	}
    // ====================================================================================
    // --- ПРОТОКОЛ ALLYSTAR / NEOWAY (БИНАРНЫЙ HEX) ---
    // ====================================================================================
    else
    {
        uint32_t congig_type_nav = (param1<<16) + param2;
        data_command[0] = 0xF1;
        data_command[1] = 0xD9;
        data_command[2] = 0x06;
        switch(command){
        case CMD_EN_DIS_MSG:
            len = 7;
            data_command[3] = 0x01;
            data_command[4] = 0x03;
            data_command[5] = 0x00;
            data_command[6] = 0xF0;
            data_command[7] = param1;
            data_command[8] = param2;
            break;
        case CMD_CONF_NAV_SYS:
            len = 8;
            data_command[3] = 0x0C;
            data_command[4] = 0x04;
            data_command[5] = 0x00;
            data_command[6] = congig_type_nav;
            data_command[7] = congig_type_nav>>8;
            data_command[8] = congig_type_nav>>16;
            data_command[9] = congig_type_nav>>24;
            break;
        case CMD_TYPE_START:
            len = 5;
            data_command[3] = 0x40;
            data_command[4] = 0x01;
            data_command[5] = 0x00;
            data_command[6] = param1;
            break;
        case CMD_SAVE_CONFIG:
            len = 12;
            data_command[3]  = 0x09;
            data_command[4]  = 0x08;
            data_command[5]  = 0x00;
            data_command[6]  = 0x00;
            data_command[7]  = 0x00;
            data_command[8]  = 0x00;
            data_command[9]  = 0x00;
            data_command[10] = 0x2F;
            data_command[11] = 0x00;
            data_command[12] = 0x00;
            data_command[13] = 0x00;
            data_command[14] = 0x46;
            data_command[15] = 0xB7;
            break;
        }
        Gps_Crc(&data_command[2], len);
        len = len + 4;
        transmit_buffer(data_command, len, TYPE_GPS);
        memset(data_command, 0, len+4);
    }
}


//===========================================================================================================================
#if TEST_GPS
//// Прямоугольник ~130x130 метров для проверки чанков >127м
//// Точка 0: Старт
//// Точка 1: +130м на Север
//// Точка 2: +130м на Восток
//// Точка 3: -130м на Юг
//// Точка 4: -130м на Запад (возврат в старт)
//static const double test_lats[] = {
//    55.750000,
//    55.751170,
//    55.751170,
//    55.750000,
//    55.750000
//};
//
//static const double test_lons[] = {
//    37.620000,
//    37.620000,
//    37.622080,
//    37.622080,
//    37.620000
//};

// Прямоугольник ~15x22 метра для проверки чанков (малые расстояния)
// Точка 0: Старт
// Точка 1: +15м на Север
// Точка 2: +22м на Восток
// Точка 3: -15м на Юг
// Точка 4: -22м на Запад (возврат в старт)
static const double test_lats[] = {
    55.7332645,
    55.7333995,  // +15м по широте
    55.7333995,
    55.7332645,
    55.7332645
};

static const double test_lons[] = {
    37.6461728,
    37.6461728,
    37.6465238,  // +22м по долготе
    37.6465238,
    37.6461728
};

static uint8_t test_gps_step = 0;
#endif

#include <stdlib.h>
#include <math.h>

//===========================================================================================================================
// Преобразование строки NMEA (DDMM.MMMMM или DDDMM.MMMMM) в int32_t (градусы × 10 000 000)
// hemisphere: 'N', 'S', 'E', 'W'
//===========================================================================================================================
static double_t NmeaToDecimalDegrees(double_t nmea_coord)
{
	double Degree = 0;
	double Minute = 0;
	double_t degrees = 0;

	Minute = modf(nmea_coord/100.0, &Degree)*100.0;
	degrees = (Degree + (Minute/60.0));

    // Переводим в десятичные градусы
    return degrees;
}

void Gps_Receive(void)
{
	static uint8_t soft_rate_counter = 0;
    uint16_t len = 0;
    if (get_state_init_flag_gps() == 0) {
        return;
    }

#if 0 // TEST_GPS
    uint8_t * data_gps = test_gps;
#else
    uint8_t * data_gps = buffer_uart_gps.receive;
#endif

    if (buffer_uart_gps.TimeFlagRX) {
        buffer_uart_gps.TimeFlagRX = 0;

        if (strstr((char*)data_gps, "GLL") != NULL) {
            // === ПРОГРАММНЫЙ ФИЛЬТР НА 10 СЕКУНД ДЛЯ LOCOSYS ===
            if (Gps_Chip_Type == CHIP_LOCOSYS_AIROHA_NEW){
                soft_rate_counter++;

                // Если 10 секунд еще не прошло — просто очищаем буфер и выходим.
                // Никаких вычислений atof и отправки в LoRa не происходит.
                if (soft_rate_counter < 10){
                    len = buffer_uart_gps.count_byter_r;
                    buffer_uart_gps.count_byter_r = 0;
                    memset(&buffer_uart_gps.receive, 0, len);
                    Gps_Receive_flag_set(0);
                    return;
                }
                soft_rate_counter = 0;
            }


            parsing_gll(data_gps);
#if CONFIG_LORA
                g_lat_scaled = NmeaToDecimalDegrees(Latitude) * 10000000.0;
                g_lon_scaled = NmeaToDecimalDegrees(Longitude) * 10000000.0;
#if TEST_GPS
                // ПРЯМАЯ ПОДМЕНА МАСШТАБИРОВАННЫХ КООРДИНАТ ДЛЯ ТЕСТА
                // Умножаем тестовые double на 1e7, чтобы они соответствовали формату Lora_UpdateGPSHistory
                g_lat_scaled = (int32_t)(test_lats[test_gps_step] * 10000000.0);
                g_lon_scaled = (int32_t)(test_lons[test_gps_step] * 10000000.0);

                test_gps_step++;
                if (test_gps_step >= 5) {
                    test_gps_step = 0;
                }
#endif
#endif
            // Проверка валидности и ненулевых координат
            if ((g_lat_scaled != 0) && (g_lon_scaled != 0)) {
                Gps_flag_navigation = 1;
            }
            else {
                Gps_flag_navigation = 0;
            }
#if CONFIG_LORA
			// Вызов обновления GPS History с уже готовыми int32_t значениями
			Lora_UpdateGPSTrack(g_lat_scaled, g_lon_scaled);
#endif
			buffer_uart_gps.TimerReceive = GetTick();
			Gps_flag_err = 0;
        }

        len = buffer_uart_gps.count_byter_r;
        buffer_uart_gps.count_byter_r = 0;
        memset(&buffer_uart_gps.receive[0], 0, len);
        Gps_Receive_flag_set(0);
    }
    uint32_t TimerErrRreceive = GetTick() - buffer_uart_gps.TimerReceive;
    if(TimerErrRreceive > 20000){
    	Gps_flag_err = 1;
    }
}

//===========================================================================================================================
void parsing_gga(uint8_t *data)
{
		uint16_t pos = 0;
		pos = Search_text(data, "GGA");
		pos += Search_text(&data[pos], ",");
		//
		Time_coord  = atof(&data[pos]);
		pos += Search_text(&data[pos], ",");
		//
		Latitude  = atof(&data[pos]);

		pos += Search_text(&data[pos], ",");
		pos += Search_text(&data[pos], ",");
		//
		Longitude = atof(&data[pos]);
}

void parsing_gll(uint8_t *data)
{
		uint16_t pos = 0;
		pos = Search_text(data, "GLL");
		pos += Search_text(&data[pos], ",");
		//
		Latitude  = atof(&data[pos]);

		pos += Search_text(&data[pos], ",");
		if(data[pos] == 'S'){
			Latitude *= (-1);
		}
		pos += Search_text(&data[pos], ",");
		//
		Longitude = atof(&data[pos]);

		pos += Search_text(&data[pos], ",");
		if(data[pos] == 'W'){
			Longitude *= (-1);
		}
		pos += Search_text(&data[pos], ",");
		//
		Time_coord  = atof(&data[pos]);
}

void parsing_rmc(uint8_t *data)
{
		uint16_t pos = 0;
		pos = Search_text(data, "RMC");
		pos += Search_text(&data[pos], ",");
		//
		Time_coord  = atof(&data[pos]);
		pos += Search_text(&data[pos], ",");
		pos += Search_text(&data[pos], ",");
		//
		Latitude  = atof(&data[pos]);

		pos += Search_text(&data[pos], ",");
		pos += Search_text(&data[pos], ",");
		//
		Longitude = atof(&data[pos]);
}

#endif
