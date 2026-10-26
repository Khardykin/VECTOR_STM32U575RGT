/*
 * gps.c
 *
 *  Created on: 2024 Sep 2
 *      Author: Dmitriy
 */
#include "Vector_main.h"
#include "Gps.h"
#if CONFIG_GPS

#include <string.h>   /* memset/strncmp ( strstr/strlen - legacy parsing_*) */
#include <stdio.h>    /* sprintf (ветка TEST_GPS) */

static uint16_t Gps_Crc(uint8_t *data, uint16_t count);
static double_t NmeaToDecimalDegrees(double_t nmea_coord);
void Uart_Gps_Set_Baudrate(uint32_t baud);
uint8_t Gps_Auto_Detect_Chip(void);
/* NMEA-разбор LOCOSYS по спеке (GGA + GSV, 1 Гц) - определения перед Gps_Receive */
static const char *Nmea_Field(const char *s, uint8_t n);
static uint8_t Nmea_Field_Empty(const char *f);
static uint8_t Nmea_IsType(const char *p, const char *type);
static uint8_t Nmea_SentenceComplete(const char *p, const char *end);
static void Gps_ParseGga(const char *s);
static void Gps_CommitSecond(void);
static void Gps_CollectGsv(const char *s);
static void Gps_UpdateSignal(void);
static void Gps_Publish_Coords(void);
//===========================================================================================================================
static DETECT_STATE_t Gps_Detect_State = DETECT_STATE_START_115200;
static uint32_t 		Gps_Detect_Timeout_Timer = 0;
GNSS_CHIP_TYPE			Gps_Chip_Type = CHIP_ALLYSTAR_OLD;
static uint8_t 			Gps_Verification_Mode = 0;
static uint8_t 			Gps_flag_err = 0;
static uint8_t 			Gps_Init_flag = 0;
static uint8_t			Gps_flag_receive = 0;
static uint8_t			Gps_flag_navigation = 0;
//===========================================================================================================================
Message_buffer_uart_gps buffer_uart_gps = {0};
double_t Time_coord = 0;
double_t Latitude = 0;
double_t Longitude = 0;
int32_t g_lat_scaled = 0;
int32_t g_lon_scaled = 0;
uint32_t Gps_type_nav_pre = 0;
/* Выходы спеки LOCOSYS (GGA/GSV): спутники из GGA (поле 7) и сигнал дБ-Гц -
   среднее четырёх лучших C/No из GSV за секунду; 0 при отсутствии фикса.   */
int   Gps_satellites_used = 0;
float Gps_signal_db_hz = 0.0f;
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
/* Приём байта из ISR: кадр копится от '$' до тишины TIME_OUT_GPS (50 мс -
   Uart_Gps_Receive_Timer_Inc взведёт TimeFlagRX). В рабочем режиме в буфер
   попадают ТОЛЬКО предложения GGA/GSV (спека - общая для LOCOSYS и
   Allystar): тип предложения решается по его 6-му байту (символы 3..5 после
   '$'), остальные предложения ОТКАТЫВАЮТСЯ - не занимают место в буфере.
   В режиме верификации (Gps_Auto_Detect_Chip) поток пишется СЫРЬЁМ - детект
   смотрит на первый символ ответа. КОНТЕКСТ: ISR приёма UART.              */
void Gps_Data_Verification(uint8_t data)
{
	/* Состояние текущего предложения: индекс его начала в receive[] и вердикт
	   (2 = тип ещё не определён, 1 = пишем, 0 = отброшено).                  */
	static uint16_t sent_start = 0;
	static uint8_t  sent_keep = 2;

	if((Gps_Verification_Mode == 0) && (get_state_init_flag_gps() == 0)){
		return;
	}

	if(data == '$'){
		if((Gps_Receive_flag_read() == 0)){
			/* первый '$' после тишины - новый кадр */
			Gps_Receive_flag_set(1);
			buffer_uart_gps.count_byter_r = 0;
			sent_start = 0;
		}
		else{
			sent_start = buffer_uart_gps.count_byter_r;   /* следующее предложение */
		}
		sent_keep = 2;
	}

	if(Gps_Receive_flag_read() == 1){
		if(Gps_Verification_Mode == 0){
			buffer_uart_gps.TimeRX = TIME_OUT_GPS;    /* байт есть - тишина сбрасывается */
		}

		if(buffer_uart_gps.count_byter_r < (uint16_t)(sizeof(buffer_uart_gps.receive) - 1u)){
			uint16_t idx = buffer_uart_gps.count_byter_r;

			if((Gps_Verification_Mode != 0) || (sent_keep != 0u)){
				buffer_uart_gps.receive[idx] = data;
				buffer_uart_gps.count_byter_r = (uint16_t)(idx + 1u);
			}

			/* Вердикт предложению - на его 6-м байте: символы 3..5 от '$'
			   ("GGA"/"GSV" - спека, общая для LOCOSYS и Allystar). Чужое
			   предложение - откат записи.                                 */
			if((Gps_Verification_Mode == 0) && (sent_keep == 2u) &&
			   ((uint16_t)(idx + 1u - sent_start) >= 6u) &&
			   (buffer_uart_gps.count_byter_r >= (uint16_t)(sent_start + 6u))){
				const char *t = (const char *)&buffer_uart_gps.receive[sent_start + 3u];
				if((strncmp(t, "GGA", 3) == 0) || (strncmp(t, "GSV", 3) == 0)){
					sent_keep = 1u;
				}
				else{
					sent_keep = 0u;
					/* откат: обнулить записанный кусок - за count_byter_r должны
					   оставаться только '\0', иначе разбор кадра прочтёт «призраки»
					   отброшенного предложения (регион мал, <= длина строки NMEA) */
					memset(&buffer_uart_gps.receive[sent_start], 0,
						   (size_t)(buffer_uart_gps.count_byter_r - sent_start));
					buffer_uart_gps.count_byter_r = sent_start;
				}
			}
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
			memset(&buffer_uart_gps.receive, 0, sizeof(buffer_uart_gps.receive));

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
		// 1. Спека LOCOSYS: в потоке обрабатываются ТОЛЬКО GGA и GSV - их и
		//    включаем (аппаратный ритм модуля 1 с - его максимум; param2 у
		//    LOCOSYS - вкл/выкл, lscfg_action в Gps_Transmit, не период).
		//    GGA: качество фикса (поле 6), спутники (поле 7), координаты.
		//    GSV: уровни C/No для сигнала («золотая четвёрка»).
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_GGA, 1); Delay(25);
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_GSA, 0); Delay(25);
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_GSV, 1); Delay(25);
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_RMC, 0); Delay(25);
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_VTG, 0); Delay(25);
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_ZDA, 0); Delay(25);

		// 2. GLL больше не нужен: навигация берётся из GGA («нет координат»
		//    определяется ПУСТЫМИ полями и качеством фикса, а не нулевой
		//    координатой), а ритм 10 с для координат держит программный
		//    гейт в Gps_Receive (Gps_Publish_Coords), не модуль.
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_GLL, 0); Delay(25);

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
		// Инициализируем бинарный протокол Allystar на скорости 9600.
		// Та же спека, что у LOCOSYS: GGA + GSV КАЖДУЮ СЕКУНДУ (param2 у
		// Allystar - период в секундах, 0 = выкл), GLL выключен - навигация
		// берётся из GGA, ритм 10 с для координат держит программный гейт
		// Gps_Publish_Coords. Бюджет канала: GGA+GSV на 1 Гц - до ~650 Б/с,
		// 9600 бод (~960 Б/с) вмещают.
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_GGA, 1);
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_GLL, 0);
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_GSA, 0);
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_GRS, 0);
		Gps_Transmit(CMD_EN_DIS_MSG, TYPE_MSG_GSV, 1);
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
                    sprintf(temp_str, "LSSCFGSYS,%lu", (unsigned long)sysMask);
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

/* ===========================================================================
 * NMEA-разбор по спеке (GGA + GSV, аппаратный ритм 1 Гц - ОБЩЕЕ для чипов
 * LOCOSYS и Allystar, оба настраиваются в Gps_Init):
 *   - «отсутствие» параметра определяется ПУСТЫМ полем (следующие друг за
 *     другом ','), а не нулевым значением координаты (прежняя проверка
 *     g_lat_scaled != 0 путала «нет фикса» и «координата ровно 0»);
 *   - координаты В СИСТЕМУ (g_lat_scaled/g_lon_scaled + трек LoRa) публикуются
 *     СТРОГО раз в GPS_COORD_PUBLISH_MS (10 с) - программный гейт вместо
 *     прежнего счётчика кадров; сами GGA/GSV обрабатываются КАЖДУЮ секунду,
 *     чтобы усреднение сигнала не искажалось (у Allystar то же самое,
 *     период у сообщения задаётся в Gps_Init, а не «железом» модуля);
 *   - нет фикса: широта/долгота строго 0.0, Gps_signal_db_hz строго 0;
 *   - кадр ПРИЁМНОГО БУФЕРА - не обязательно одна секунда: при задержке
 *     receiver'а в нём копится несколько секунд подряд, а при переполнении
 *     буфера хвост обрезан. Поэтому разбор ПОПРЕДЛОЖЕНЧЕСКИЙ с проверкой
 *     полноты ('*'/перевод строки);
 *   - АРХИТЕКТУРА ЗАКРЫТИЯ (требование автора): цикл Gps_Receive только
 *     ПАРСИТ - GGA пишется в ТЕНЬ секунды (s_sec; следующая GGA перезаписывает
 *     предыдущую), GSV копятся в s_snr (сброс сборщика СТРОГО на новой GGA -
 *     границе секунды, накопление внутри секунды не прерывается). Тяжёлые
 *     Gps_UpdateSignal()/Gps_Publish_Coords() вызываются СТРОГО ОДИН РАЗ -
 *     на выходе из Gps_Receive, после цикла: в многосекундном кадре нет
 *     двойных срабатываний, итог - по последней полной секунде, среднее
 *     C/No разных секунд не смешивается.
 * КОНТЕКСТ: поток receiver_task (вызывается из Gps_Receive).
 * ======================================================================== */
#define GPS_COORD_PUBLISH_MS   (10000u)  /* интервал выдачи навигационной точки */
#define GPS_SNR_MAX            (48u)     /* C/No за секунду: до 12 GSV x 4 спутника */

static uint8_t  s_snr[GPS_SNR_MAX];      /* уровни C/No текущей секунды       */
static uint8_t  s_snr_cnt = 0;
static uint32_t s_coord_pub_tick = 0;    /* тик последней публикации координат */
static uint8_t  s_coord_pub_once = 0;    /* 1 = публикация уже была (первый фикс - сразу) */

/* ТЕНЬ последней секунды кадра (требование автора): цикл Gps_Receive только
   ПАРСИТ предложения сюда, каждая следующая GGA перезаписывает предыдущую
   секунду целиком; коммит в глобальные переменные (Gps_CommitSecond) и тяжёлые
   закрытия (Gps_UpdateSignal/Gps_Publish_Coords) - СТРОГО ОДИН РАЗ на выходе
   из Gps_Receive, после цикла. В многосекундном кадре (задержка receiver'а)
   двойных вызовов закрытия нет, состояние - по последней полной секунде.    */
typedef struct
{
	uint8_t  valid;     /* в кадре была GGA (секунда закрыта)                  */
	uint8_t  nav;       /* фикс: качество > 0 И спутников >= 4 И координаты не пусты */
	uint8_t  has_utc;   /* поле времени GGA не пустое                          */
	int      qual;      /* поле 6 GGA - качество фикса                         */
	int      sats;      /* поле 7 GGA - число спутников                        */
	double_t utc;       /* поле 1 GGA - UTC hhmmss.ss                          */
	double_t lat;       /* DDMM.MMMM со знаком полушария (формат Latitude)     */
	double_t lon;
} gps_second_t;

static gps_second_t s_sec = {0};
#if CONFIG_LORA
static uint8_t  s_lora_loss_sent = 0;    /* 1 = потеря фикса уже сообщена LoRa (край) */
#endif

/* Указатель на начало поля номер n NMEA-предложения (0 = заголовок "$GxGGA").
   Граница поля - ','; конец предложения - '*' (контрольная сумма), '\r', '\n',
   '\0' и '$' (кадр в буфере - несколько предложений подряд, без разбиения на
   строки). NULL = в предложении меньше n полей.                             */
static const char *Nmea_Field(const char *s, uint8_t n)
{
	/* Старт ЗА ведущим '$' - иначе '$' (стоп-символ) остановил бы поиск
	   первого же поля. От перескока на следующее предложение скан защищают
	   '*' (контрольная сумма) и '\r'/'\n' - предложение всегда ими кончается;
	   '$' в стоп-наборе - страховка для потока без контрольной суммы.       */
	const char *p = (*s == '$') ? (s + 1) : s;
	uint8_t i;

	for (i = 0; i < n; i++) {
		while ((*p != ',') && (*p != '*') && (*p != '$') &&
			   (*p != '\r') && (*p != '\n') && (*p != '\0')) {
			p++;
		}
		if (*p != ',') {
			return NULL;            /* предложение кончилось раньше поля n */
		}
		p++;
	}
	return p;
}

/* Поле ПУСТОЕ (параметр отсутствует): при отсутствии значения NMEA выдаёт
   ',' подряд. Так «нет координат» отличается от «координата равна нулю».   */
static uint8_t Nmea_Field_Empty(const char *f)
{
	if (f == NULL) {
		return 1u;
	}
	return (uint8_t)((*f == ',') || (*f == '*') || (*f == '$') ||
					 (*f == '\r') || (*f == '\n') || (*f == '\0'));
}

/* Тип предложения у '$': три символа со смещением 3 (напр. "GGA"). Безопасно
   для оборванного предложения в конце буфера ('\0' не совпадёт с символом). */
static uint8_t Nmea_IsType(const char *p, const char *type)
{
	uint8_t i;

	for (i = 0; i < 3u; i++) {
		if (p[3u + i] != type[i]) {
			return 0u;
		}
	}
	return 1u;
}

/* Полнота предложения: ищем в [p, end) '*' (контрольная сумма - есть всегда
   у LOCOSYS/Allystar) или '\n' (конец строки - страховка для потока без
   контрольной суммы). Обрезанный хвост при переполнении буфера не имеет
   ни того, ни другого - такой огрызок НЕ парсится (мусор хуже потери).    */
static uint8_t Nmea_SentenceComplete(const char *p, const char *end)
{
	while ((p < end) && (*p != '\0')) {
		if ((*p == '*') || (*p == '\n')) {
			return 1u;
		}
		p++;
	}
	return 0u;
}

/* GGA по спеке: поле 2/3 = широта + N/S, 4/5 = долгота + E/W, 6 = качество
   фикса, 7 = число спутников. Качество == 0/пусто ИЛИ спутников < 4 ИЛИ поля
   координат пусты -> связи нет: gps_flag_navigation = 0, широта/долгота
   СТРОГО 0.0 (и масштабированные для LoRa тоже). Фикс есть -> флаг = 1,
   Latitude/Longitude обновляются каждую секунду (в системе - см. гейт 10 с
   в Gps_Publish_Coords). Формат Latitude/Longitude прежний (как parsing_gll):
   NMEA DDMM.MMMM со знаком полушария.                                      */
static void Gps_ParseGga(const char *s)
{
	const char *f_time = Nmea_Field(s, 1);
	const char *f_lat  = Nmea_Field(s, 2);
	const char *f_ns   = Nmea_Field(s, 3);
	const char *f_lon  = Nmea_Field(s, 4);
	const char *f_ew   = Nmea_Field(s, 5);
	const char *f_qual = Nmea_Field(s, 6);
	const char *f_sats = Nmea_Field(s, 7);

	/* Каждая GGA ПЕРЕЗАПИСЫВАЕТ тень секунды ЦЕЛИКОМ (в многосекундном кадре
	   вторая секунда просто затирает первую). ПУСТОЕ поле (',' подряд) никогда
	   не доходит до atoi/atof - сначала проверка Nmea_Field_Empty, затем
	   Nmea_Field вернул бы NULL для несуществующего поля: сдвигов индексов и
	   чтения мусора нет (защита п.3 требования автора).                      */
	s_sec.valid   = 1u;
	s_sec.qual    = (Nmea_Field_Empty(f_qual) != 0u) ? 0 : atoi(f_qual);
	s_sec.sats    = (Nmea_Field_Empty(f_sats) != 0u) ? 0 : atoi(f_sats);
	s_sec.has_utc = (uint8_t)((Nmea_Field_Empty(f_time) == 0u) ? 1u : 0u);
	s_sec.utc     = (s_sec.has_utc != 0u) ? atof(f_time) : 0;
	s_sec.lat     = 0;
	s_sec.lon     = 0;

	if ((s_sec.qual <= 0) || (s_sec.sats < 4) ||
		(Nmea_Field_Empty(f_lat) != 0u) || (Nmea_Field_Empty(f_ns) != 0u) ||
		(Nmea_Field_Empty(f_lon) != 0u) || (Nmea_Field_Empty(f_ew) != 0u)) {
		s_sec.nav = 0u;             /* связи нет - координаты не извлекаем      */
		return;
	}

	s_sec.nav = 1u;
	s_sec.lat = atof(f_lat);
	if (*f_ns == 'S') {
		s_sec.lat = -s_sec.lat;
	}
	s_sec.lon = atof(f_lon);
	if (*f_ew == 'W') {
		s_sec.lon = -s_sec.lon;
	}
}

/* Коммит тени ПОСЛЕДНЕЙ секунды кадра в глобальные переменные - строго один
   раз на кадр (зовётся на выходе Gps_Receive). ИНВАРИАНТ потери фикса (п.4
   требования автора): qual == 0 ИЛИ sats < 4 -> Gps_flag_navigation = 0,
   Latitude/Longitude/g_lat_scaled/g_lon_scaled/Gps_signal_db_hz - СТРОГО 0,
   10-секундный счётчик публикации сброшен (после возврата фикса координаты
   уйдут сразу). КОНТЕКСТ: receiver_task.                                    */
static void Gps_CommitSecond(void)
{
	Gps_satellites_used = s_sec.sats;
	Gps_flag_navigation = s_sec.nav;

	if (s_sec.nav == 0u) {
		Latitude     = 0;
		Longitude    = 0;
		g_lat_scaled = 0;
		g_lon_scaled = 0;
		Gps_signal_db_hz = 0.0f;    /* подтвердится и в Gps_UpdateSignal      */
		s_coord_pub_once = 0u;      /* сброс 10-с счётчика                    */
		return;
	}

	if (s_sec.has_utc != 0u) {
		Time_coord = s_sec.utc;
	}
	Latitude  = s_sec.lat;          /* текущие значения - каждую секунду;     */
	Longitude = s_sec.lon;          /* в систему - раз в 10 с (Publish)       */
}

/* GSV: сбор C/No (дБ-Гц) - 4-е поле каждого блока спутника (индексы полей
   предложения 7, 11, 15, 19). Пустое поле = спутник виден, но не сопровождается
   - ПРОПУСКАЕМ (нуль исказил бы среднее). За секунду может прийти несколько
   GSV разных созвездий - копим в s_snr, расчёт в Gps_UpdateSignal.          */
static void Gps_CollectGsv(const char *s)
{
	uint8_t idx;

	for (idx = 7u; idx <= 19u; idx = (uint8_t)(idx + 4u)) {
		const char *f = Nmea_Field(s, idx);
		if (f == NULL) {
			break;                  /* в предложении больше нет блоков */
		}
		if (Nmea_Field_Empty(f) != 0u) {
			continue;
		}
		if (s_snr_cnt < GPS_SNR_MAX) {
			s_snr[s_snr_cnt] = (uint8_t)atoi(f);
			s_snr_cnt++;
		}
	}
}

/* «Золотая четвёрка»: четыре лучших C/No секунды (частичная сортировка
   убыванием - полный sort не нужен), среднее арифметическое с округлением
   до 0.1 дБ-Гц (целочисленно). КРИТИЧЕСКОЕ условие спеки: нет фикса ->
   Gps_signal_db_hz ПРИНУДИТЕЛЬНО 0.                                        */
static void Gps_UpdateSignal(void)
{
	uint32_t sum = 0;
	uint8_t  k;
	uint8_t  i;
	uint8_t  j;

	if ((Gps_flag_navigation == 0u) || (s_snr_cnt == 0u)) {
		Gps_signal_db_hz = 0.0f;
		return;
	}

	k = (s_snr_cnt < 4u) ? s_snr_cnt : 4u;
	for (i = 0; i < k; i++) {
		uint8_t best = i;
		for (j = (uint8_t)(i + 1u); j < s_snr_cnt; j++) {
			if (s_snr[j] > s_snr[best]) {
				best = j;
			}
		}
		if (best != i) {
			uint8_t t = s_snr[i];
			s_snr[i] = s_snr[best];
			s_snr[best] = t;
		}
		sum += s_snr[i];
	}
	/* x10, округление, /k, /10 - один знак после запятой без float-шума */
	Gps_signal_db_hz = (float)((sum * 10u + (uint32_t)(k / 2u)) / (uint32_t)k) / 10.0f;
}

/* Публикация координат В СИСТЕМУ - строго раз в GPS_COORD_PUBLISH_MS (10 с;
   программный ритм поверх аппаратной 1 Гц). Нет фикса - публиковать нечего:
   координаты уже обнулены в Gps_ParseGga, трек нулями НЕ кормим (у
   Lora_UpdateGPSTrack gps_last_valid_* должен оставаться последней ВАЛИДНОЙ
   точкой), а гейт сбрасывается - первый же фикс опубликуется сразу.
   КОНТЕКСТ: receiver_task (каждую секунду, решение - по тику).              */
static void Gps_Publish_Coords(void)
{
	if (Gps_flag_navigation == 0u) {
#if CONFIG_LORA
		/* Потеря фикса: сообщаем LoRa ОДИН раз за пропажу (край) - флаг
		   навигации там переключается по flag_navigation, а не по проверке
		   «координаты != 0». Трек нулями не кормим, gps_last_valid_* на
		   стороне LoRa сохраняет последнюю ВАЛИДНУЮ точку.                */
		if (s_lora_loss_sent == 0u) {
			s_lora_loss_sent = 1u;
			Lora_UpdateGPSTrack(0, 0, 0);
		}
#endif
		s_coord_pub_once = 0u;      /* первый фикс после потери - сразу */
		return;
	}
#if CONFIG_LORA
	s_lora_loss_sent = 0u;
#endif
	if ((s_coord_pub_once != 0u) &&
		((GetTick() - s_coord_pub_tick) < GPS_COORD_PUBLISH_MS)) {
		return;                     /* 10 с ещё не прошло */
	}
	s_coord_pub_tick = GetTick();
	s_coord_pub_once = 1u;


	g_lat_scaled = (int32_t)(NmeaToDecimalDegrees(Latitude)  * 10000000.0);
	g_lon_scaled = (int32_t)(NmeaToDecimalDegrees(Longitude) * 10000000.0);
#if TEST_GPS
	/* ПРЯМАЯ ПОДМЕНА масштабированных координат для теста (формат 1e-7 градуса,
	   как ждёт Lora_UpdateGPSHistory) */
	g_lat_scaled = (int32_t)(test_lats[test_gps_step] * 10000000.0);
	g_lon_scaled = (int32_t)(test_lons[test_gps_step] * 10000000.0);

	test_gps_step++;
	if (test_gps_step >= 5) {
		test_gps_step = 0;
	}
#endif
#if CONFIG_LORA
	/* Точка трека - раз в 10 с (внутри Lora_UpdateGPSTrack свой дополнительный
	   делитель LORA_CFG_GET_GPSFIX); flag_navigation=1 - фикс есть.        */
	Lora_UpdateGPSTrack(g_lat_scaled, g_lon_scaled, 1);
#endif
}

void Gps_Receive(void)
{
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

        /* === СПЕКА NMEA (ОБЩАЯ для LOCOSYS и Allystar - оба чипа настроены
           в Gps_Init на GGA + GSV каждую секунду): обрабатываются только
           предложения GGA и GSV, остальные пропускаются (их и в буфере нет -
           фильтр ISR Gps_Data_Verification).

           Кадр - НЕ обязательно одна секунда: при задержке receiver'а в
           буфере копится несколько секунд подряд, а при переполнении
           (768 Б ~ 2-3 с потока) хвост обрезан. Обход ПОПРЕДЛОЖЕНЧЕСКИЙ:
             - предложение = от '$' до следующего '$' / конца буфера;
             - неполное (нет '*' и перевода строки - обрезанный хвост)
               ПРОПУСКАЕТСЯ: парсить огрызок хуже, чем потерять;
             - GGA -> парсинг В ТЕНЬ s_sec (следующая GGA перезаписывает
               предыдущую секунду целиком) + сброс сборщика C/No: граница
               секунды отмечается СТРОГО на заголовке новой GGA;
             - GSV -> накопление C/No (только после первой GGA кадра: без
               неё «владение» секундой неизвестно).
           Цикл ТОЛЬКО ПАРСИТ: закрытие секунды (коммит тени + сигнал +
           публикация) - СТРОГО ОДИН РАЗ ниже, на выходе из функции. === */
        const char *p = (const char *)data_gps;

        s_snr_cnt   = 0;
        s_sec.valid = 0u;                   /* тень секунды - с чистого листа */

        while (*p != '\0') {
            const char *end;

            if (*p != '$') {
                p++;
                continue;
            }
            end = p + 1;                    /* граница: следующий '$' или '\0' */
            while ((*end != '\0') && (*end != '$')) {
                end++;
            }

            if (Nmea_SentenceComplete(p, end) != 0u) {
                if (Nmea_IsType(p, "GGA") != 0u) {
                    Gps_ParseGga(p);        /* в тень: секунда перезаписывается */
                    s_snr_cnt = 0;          /* НОВАЯ GGA = новая секунда: сбор C/No заново */
                }
                else if ((Nmea_IsType(p, "GSV") != 0u) && (s_sec.valid != 0u)) {
                    Gps_CollectGsv(p);      /* накопление C/No текущей секунды */
                }
            }
            p = end;
        }

        /* ЗАКРЫТИЕ СЕКУНДЫ - СТРОГО ОДИН РАЗ на кадр, ПОСЛЕ цикла (требование
           автора): даже если в буфер натекло 2-3 секунды, Gps_UpdateSignal и
           Gps_Publish_Coords вызываются один раз - по последней полной
           секунде; 10-секундный гейт публикации от этого не «тикает» лишнего.
           Кадр без единой полной GGA (обрезок) globals НЕ трогает.          */
        if (s_sec.valid != 0u) {
            Gps_CommitSecond();             /* тень -> globals (инвариант обнуления) */
            Gps_UpdateSignal();             /* «золотая четвёрка»; нет фикса -> 0 */
            Gps_Publish_Coords();           /* в систему - раз в 10 с (гейт по тику) */
        }

        buffer_uart_gps.TimerReceive = GetTick();
        Gps_flag_err = 0;

        buffer_uart_gps.count_byter_r = 0;
        /* чистим ВЕСЬ накопитель (а не len байт): гарантия инварианта «за
           count только '\0'» при любом исходе (откаты предложений в ISR,
           переполнение, переход из режима верификации). КОНТЕКСТ: поток.   */
        memset(&buffer_uart_gps.receive[0], 0, sizeof(buffer_uart_gps.receive));
        Gps_Receive_flag_set(0);
    }
    uint32_t TimerErrRreceive = GetTick() - buffer_uart_gps.TimerReceive;
    if(TimerErrRreceive > 20000){
    	Gps_flag_err = 1;
    }
}

//=========================================================================================================
void parsing_gga(uint8_t *data)
{
		uint16_t pos = 0;
		pos = Search_text(data, "GGA");
		pos += Search_text(&data[pos], ",");
		//
		Time_coord  = atof((char *)&data[pos]);
		pos += Search_text(&data[pos], ",");
		//
		Latitude  = atof((char *)&data[pos]);

		pos += Search_text(&data[pos], ",");
		pos += Search_text(&data[pos], ",");
		//
		Longitude = atof((char *)&data[pos]);
}

void parsing_gll(uint8_t *data)
{
		uint16_t pos = 0;
		pos = Search_text(data, "GLL");
		pos += Search_text(&data[pos], ",");
		//
		Latitude  = atof((char *)&data[pos]);

		pos += Search_text(&data[pos], ",");
		if(data[pos] == 'S'){
			Latitude *= (-1);
		}
		pos += Search_text(&data[pos], ",");
		//
		Longitude = atof((char *)&data[pos]);

		pos += Search_text(&data[pos], ",");
		if(data[pos] == 'W'){
			Longitude *= (-1);
		}
		pos += Search_text(&data[pos], ",");
		//
		Time_coord  = atof((char *)&data[pos]);
}

void parsing_rmc(uint8_t *data)
{
		uint16_t pos = 0;
		pos = Search_text(data, "RMC");
		pos += Search_text(&data[pos], ",");
		//
		Time_coord  = atof((char *)&data[pos]);
		pos += Search_text(&data[pos], ",");
		pos += Search_text(&data[pos], ",");
		//
		Latitude  = atof((char *)&data[pos]);

		pos += Search_text(&data[pos], ",");
		pos += Search_text(&data[pos], ",");
		//
		Longitude = atof((char *)&data[pos]);
}

#endif
