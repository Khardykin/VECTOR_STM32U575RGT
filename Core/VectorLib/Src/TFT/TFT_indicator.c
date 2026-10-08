#include "Vector_main.h"
#if (CONFIG_TYPE_LCD == 2)   /* слой индикации Avis: ждёт переноса (SNS_CFG_Type,
                               CALIB_CFG, COUNT_CHAN, DEVICE_NUMBER Device2/3_Pro) */
#include "TFT.h"
#include "TFT_indicator.h"
#include "LCD_platform.h"
//------------------------------------------------------------------------------
static uint16_t  buf_lcd[BUF_SIZE] __attribute__((aligned(4)));
static uint16_t  buf_lcd2[BUF_SIZE] __attribute__((aligned(4)));

lv_display_t * disp;
//------------------------------------------------------------------------------
uint8_t str_lcd[30] = {0};
uint8_t str_lcd_text[100] = {0};
uint8_t len_str = 0;

//------------------------------------------------------------------------------
uint32_t color_Chan[5] = {0xFFFFFF, 0xFF0000, 0xF9D547, 0x2ECC71, 0xEFEFF4};//белый красный желтый зеленый фон
uint32_t color_background[3] = {0x215CA2, 0xFFFFFF};//синий белый
uint32_t color_text[3] = {0xFFFFFF, 0x000000};//белый черный
//------------------------------------------------------------------------------
#if ((DEVICE_NUMBER == Device2) || (DEVICE_NUMBER == Device2_1))
    const char Device_Label[2][15] = {
        [MIRAX] = "AvisX4",
        [BACS]  = "BACS"
    };
    const char SN_Label_Prefix[2][10] = {
        [MIRAX] = "AVX",
        [BACS]  = "S/N 2."
    };
#elif (DEVICE_NUMBER == Device2_2)
    #if (DEVICE_NUMBER_MODIF)
        const char Device_Label[2][15] = {
            [MIRAX] = "SigmaXR",
            [BACS]  = "BACS"
        };
        const char SN_Label_Prefix[2][10] = {
            [MIRAX] = "SXR",
            [BACS]  = "S/N 2."
        };
    #else
        const char Device_Label[2][15] = {
            [MIRAX] = "SigmaX",
            [BACS]  = "BACS"
        };
        const char SN_Label_Prefix[2][10] = {
            [MIRAX] = "SX",
            [BACS]  = "S/N 2."
        };
    #endif
#elif ((DEVICE_NUMBER == Device2_Pro) || (DEVICE_NUMBER == Device2_Pro_1))
    const char Device_Label[2][15] = {
        [MIRAX] = "AvisX4Pro",
        [BACS]  = "BACS"
    };
    const char SN_Label_Prefix[2][10] = {
        [MIRAX] = "AX4P",
        [BACS]  = "S/N 2."
    };
#elif (DEVICE_NUMBER == Device3_Pro)
    const char Device_Label[2][15] = {
        [MIRAX] = "AvisX5Pro",
        [BACS]  = "BACS"
    };
    const char SN_Label_Prefix[2][10] = {
        [MIRAX] = "AX5P",
        [BACS]  = "S/N 2."
    };
#endif
//------------------------------------------------------------------------------
typedef struct {
	uint8_t state;           // Значение из IS_STATE_SCREEN()
	lv_obj_t ** screen_obj;  // Указатель на адрес экрана (ui_Screen...)
	void (*init_func)(void); // Указатель на функцию инициализации
} screen_map_t;

static const screen_map_t scr_map[] = {
	{STATE_SCREEN_MEASURE,           &ui_ScreenMeasurements,      ui_ScreenMeasurements_screen_init},
	{STATE_SCREEN_MEASURE_TO_MENU,   &ui_ScreenMeasurements,      ui_ScreenMeasurements_screen_init},
	{STATE_SCREEN_MENU,              &ui_ScreenMenu,              ui_ScreenMenu_screen_init},
	// Info
	{STATE_SCREEN_INFO_DEVICE,       &ui_ScreenInfo,             ui_ScreenInfo_screen_init},
	// Setting
	{STATE_SCREEN_SETTING_DEVICE,    &ui_ScreenSetting,           ui_ScreenSetting_screen_init},
	{STATE_SCREEN_SETTING_CHAN,      &ui_ScreenSettingSetChannel, ui_ScreenSettingSetChannel_screen_init},
	{STATE_SCREEN_SETTING_CHAN_SET,  &ui_ScreenSettingSet,        ui_ScreenSettingSet_screen_init},
	{STATE_SCREEN_SETTING_CHAN_SET_P,&ui_ScreenSettingSet,        ui_ScreenSettingSet_screen_init},
	{STATE_SCREEN_SETTING_CHAN_SET_M_D,&ui_ScreenSettingSet,        ui_ScreenSettingSet_screen_init},
	{STATE_SCREEN_SETTING_DATA,      &ui_ScreenSettingSet,        ui_ScreenSettingSet_screen_init},
	{STATE_SCREEN_SETTING_TIME_SET,  &ui_ScreenSettingSet,        ui_ScreenSettingSet_screen_init},
	{STATE_SCREEN_SETTING_DATA_SET,  &ui_ScreenSettingSet,        ui_ScreenSettingSet_screen_init},
//	{STATE_SCREEN_SETTING_MEAS_CHAN, &ui_ScreenSettingSetChannel, ui_ScreenSettingSetChannel_screen_init},
//	{STATE_SCREEN_SETTING_MEAS_CHAN_SET,  &ui_ScreenSettingSet,        ui_ScreenSettingSet_screen_init},
	{STATE_SCREEN_SETTING_RESET_LIMIT,&ui_ScreenStart,            ui_ScreenStart_screen_init},
	{STATE_SCREEN_SETTING_LANGUAGE,  &ui_ScreenStart,             ui_ScreenStart_screen_init},
//	{STATE_SCREEN_SETTING_FACTORY,	 &ui_ScreenStart,            ui_ScreenStart_screen_init},
	// Service
	{STATE_SCREEN_SERVICE_DEVICE,    &ui_ScreenServiceMenu,       ui_ScreenServiceMenu_screen_init},
	//calib zero
	{STATE_SCREEN_CALIB_ZERO,       		&ui_ScreenStart,		ui_ScreenStart_screen_init},
	{STATE_SCREEN_CALIB_ZERO_SELECT_CHAN,  	&ui_ScreenSettingSetChannel,   ui_ScreenSettingSetChannel_screen_init},
	{STATE_SCREEN_CALIB_ZERO_START,  		&ui_ScreenStart,     	ui_ScreenStart_screen_init},
	{STATE_SCREEN_CALIB_ZERO_END,    		&ui_ScreenStart,     	ui_ScreenStart_screen_init},
	{STATE_SCREEN_CALIB_ZERO_SELECT_CHAN_MULTI, &ui_ScreenSettingSetChannel,   ui_ScreenSettingSetChannel_screen_init},
	{STATE_SCREEN_CALIB_ZERO_START_MULTI,   &ui_ScreenStart, 	 	ui_ScreenStart_screen_init},
	{STATE_SCREEN_CALIB_ZERO_END_MULTI,   	&ui_ScreenMeasurements,	ui_ScreenMeasurements_screen_init},
	// Calib span
	{STATE_SCREEN_CALIB_SPAN,       		&ui_ScreenStart,     	ui_ScreenStart_screen_init},
	{STATE_SCREEN_CALIB_SPAN_SELECT_CHAN, 	&ui_ScreenSettingSetChannel,	ui_ScreenSettingSetChannel_screen_init},
	{STATE_SCREEN_CALIB_SPAN_CONC_P,  		&ui_ScreenStart,  		ui_ScreenStart_screen_init},
	{STATE_SCREEN_CALIB_SPAN_START,    		&ui_ScreenStart,        ui_ScreenStart_screen_init},
	{STATE_SCREEN_CALIB_SPAN_END,       	&ui_ScreenStart, 		ui_ScreenStart_screen_init},
	{STATE_SCREEN_CALIB_SPAN_SELECT_CHAN_MULTI, &ui_ScreenSettingSetChannel,   ui_ScreenSettingSetChannel_screen_init},
	{STATE_SCREEN_CALIB_SPAN_START_MULTI,   &ui_ScreenStart, 		ui_ScreenStart_screen_init},
	{STATE_SCREEN_CALIB_SPAN_END_MULTI,   	&ui_ScreenMeasurements, ui_ScreenMeasurements_screen_init},
	// Reset chan
	{STATE_SCREEN_SERVICE_RESET_ERR,    &ui_ScreenStart,     		ui_ScreenStart_screen_init},
	{STATE_SCREEN_SERVICE_DEVICE_END, 	&ui_ScreenStart,       		ui_ScreenStart_screen_init},
	// option +
	{STATE_SCREEN_MEASURE_TO_COUNTDOWN, &ui_ScreenStart,  		  	ui_ScreenStart_screen_init},
	{STATE_SCREEN_COUNTDOWN_IMIT,  	 	&ui_ScreenStart,  			ui_ScreenStart_screen_init},
	{STATE_SCREEN_COUNTDOWN_SOS,  	 	&ui_ScreenStart,  			ui_ScreenStart_screen_init},
    {STATE_SCREEN_PASSWORD,          	&ui_ScreenStart,            ui_ScreenStart_screen_init},
    {STATE_SCREEN_WARM,              	&ui_ScreenStart,            ui_ScreenStart_screen_init},
	{STATE_SCREEN_WARM_AUTO_CALIB,  	&ui_ScreenMeasurements,		ui_ScreenMeasurements_screen_init},
	{STATE_SCREEN_WARM_INFO_DEVICE,     &ui_ScreenInfo,             ui_ScreenInfo_screen_init},
//	{STATE_SCREEN_FIRMWARE,     		&ui_ScreenStart,            ui_ScreenStart_screen_init},
};
//------------------------------------------------------------------------------
lv_obj_t *DisplayChannel[COUNT_CHAN];
typedef struct {
    int16_t x, y, w, h;
    const lv_font_t * font;
    bool active;
} channel_cfg_t;
// Таблица шаблонов: [ID шаблона][Номер канала 0..7]
#if (CONFIG_MODEL_LCD == 0)
static const channel_cfg_t lcd_templates[4][COUNT_CHAN] = {
	    // Template 0 (8 каналов, h=62)
	    {
	        {10, 48, 108, 62, &ui_font_number28, true},   {122, 48, 108, 62, &ui_font_number28, true},  // CH1(L) -> CH2(R)
	        {122, 114, 108, 62, &ui_font_number28, true}, {10, 114, 108, 62, &ui_font_number28, true},  // CH4(L) <- CH3(R)
	        {10, 180, 108, 62, &ui_font_number28, true},  {122, 180, 108, 62, &ui_font_number28, true}, // CH5(L) -> CH6(R)
	        {122, 244, 108, 62, &ui_font_number28, true}, {10, 244, 108, 62, &ui_font_number28, true}   // CH8(L) <- CH7(R)
	    },
	    // Template 1 (6 каналов)
	    {
	        {10, 45, 108, 80, &ui_font_number28, true},   {122, 45, 108, 80, &ui_font_number28, true},  // CH1 -> CH2
	        {122, 130, 108, 80, &ui_font_number28, true}, {10, 130, 108, 80, &ui_font_number28, true},  // CH4 <- CH3
	        {10, 215, 108, 80, &ui_font_number28, true},  {122, 215, 108, 80, &ui_font_number28, true}, // CH5 -> CH6
	        {0, 0, 0, 0, NULL, false},                    {0, 0, 0, 0, NULL, false}                     // пустые
	    },
	    // Template 2 (8 каналов, h=60)
	    {
	        {10, 64, 108, 60, &ui_font_number28, true},   {122, 64, 108, 60, &ui_font_number28, true},  // CH1 -> CH2
	        {122, 128, 108, 60, &ui_font_number28, true}, {10, 128, 108, 60, &ui_font_number28, true},  // CH4 <- CH3
	        {10, 192, 108, 60, &ui_font_number28, true},  {122, 192, 108, 60, &ui_font_number28, true}, // CH5 -> CH6
	        {122, 256, 108, 60, &ui_font_number28, true}, {10, 256, 108, 60, &ui_font_number28, true}   // CH8 <- CH7
	    },
	    // Template 3 (6 каналов)
	    {
	        {10, 65, 108, 80, &ui_font_number28, true},   {122, 65, 108, 80, &ui_font_number28, true},  // CH1 -> CH2
	        {122, 150, 108, 80, &ui_font_number28, true}, {10, 150, 108, 80, &ui_font_number28, true},  // CH4 <- CH3
	        {10, 235, 108, 80, &ui_font_number28, true},  {122, 235, 108, 80, &ui_font_number28, true}, // CH5 -> CH6
	        {0, 0, 0, 0, NULL, false},                    {0, 0, 0, 0, NULL, false}                     // пустые
	    }
};
#elif (CONFIG_MODEL_LCD == 1)
static const channel_cfg_t lcd_templates[4][COUNT_CHAN] = {
    // Каждая строка здесь: {x, y, w, h, font, visible}
    // Template 0
    {
        {10, 48, 145, 86, &ui_font_number36, true},  {165, 48, 145, 86, &ui_font_number36, true},  // CH1(L) -> CH2(R)
        {165, 144, 145, 86, &ui_font_number36, true}, {10, 144, 145, 86, &ui_font_number36, true}  // CH4(L) <- CH3(R)
    },
    // Template 1
    {
        {10, 48, 145, 86, &ui_font_number36, true},  {165, 48, 145, 86, &ui_font_number36, true},  // CH1 -> CH2
        {165, 144, 145, 86, &ui_font_number36, true}, {10, 144, 145, 86, &ui_font_number36, true}  // CH4 <- CH3
    },
    // Template 2
    {
        {10, 48, 145, 86, &ui_font_number36, true},  {165, 48, 145, 86, &ui_font_number36, true},  // CH1 -> CH2
        {165, 144, 145, 86, &ui_font_number36, true}, {10, 144, 145, 86, &ui_font_number36, true}  // CH4 <- CH3
    },
    // Template 3
    {
        {10, 48, 145, 86, &ui_font_number36, true},  {165, 48, 145, 86, &ui_font_number36, true},  // CH1 -> CH2
        {165, 144, 145, 86, &ui_font_number36, true}, {10, 144, 145, 86, &ui_font_number36, true}  // CH4 <- CH3
    }
};
#endif

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
typedef struct {
    const char * ok_key;
    const char * back_key;
    int16_t width;
    int16_t ok_x;
    int16_t back_x;
    int16_t ok_y;
    int16_t back_y;
    lv_align_t align_ok;
    lv_align_t align_back;
} ui_button_cfg_t;

static const ui_button_cfg_t btn_table[] = {
    [ROLLER_HEAD_RANGE_GAS] 	= {"Далее", "Назад", 100, 8, -8, 0, 0, LV_ALIGN_BOTTOM_RIGHT, LV_ALIGN_BOTTOM_LEFT},
    [ROLLER_HEAD_ZERO_GAS]  	= {"Далее", "Назад", 100, 8, -8, 0, 0, LV_ALIGN_BOTTOM_RIGHT, LV_ALIGN_BOTTOM_LEFT},
    [ROLLER_HEAD_CALIB_GAS] 	= {"Сохранить", "Отмена", 100, 8, -8, 0, 0, LV_ALIGN_BOTTOM_RIGHT, LV_ALIGN_BOTTOM_LEFT},
	[ROLLER_HEAD_CALIB_WAIT] 	= {"Ждите", "Ждите", 100, 8, -8, 0, 0, LV_ALIGN_BOTTOM_RIGHT, LV_ALIGN_BOTTOM_LEFT},
    [ROLLER_HEAD_LANGUAGE]  	= {"Русский", "Английский", 175, 0, 0, -30, 30, LV_ALIGN_CENTER, LV_ALIGN_CENTER},
	[ROLLER_HEAD_COUNT_CALIB]  	= {"Один канал", "Все каналы", 175, 0, 0, -30, 30, LV_ALIGN_CENTER, LV_ALIGN_CENTER},
    [ROLLER_HEAD_CONFIRM]   	= {"Подтвердить", "Назад", 175, 0, 0, -30, 30, LV_ALIGN_CENTER, LV_ALIGN_CENTER},
    [ROLLER_HEAD_RESTART]   	= {"Перезапуск сенсоров", "Заводские настройки", 220, 0, 0, -30, 30, LV_ALIGN_CENTER, LV_ALIGN_CENTER}
};

//------------------------------------------------------------------------------
// Текст для ui_LabelHeadMenu (строго по порядку enum)
static const char* const menu_labels[] = {
    [ROLLER_MENU_INFO]               = "Информация",
    [ROLLER_MENU_SETTING]            = "Настройка",
    [ROLLER_MENU_SETTING_LIMIT]      = "Настр. порог. знач.",
    [ROLLER_MENU_SETTING_DATA_TIME]  = "Дата и время",
    [ROLLER_MENU_SETTING_RESET_STH]  = "Сброс STEL, TWA, HIGH",
    [ROLLER_MENU_SETTING_LANG]       = "Выбор языка",
    [ROLLER_MENU_SETTING_FACTORY]  	 = "Завод. настройки",
    [ROLLER_MENU_SERVICE]            = "Сервисное меню",
    [ROLLER_MENU_SERVICE_CALIB_ZERO] = "Калибровка нуля",
    [ROLLER_MENU_SERVICE_CALIB_RANGE]= "Калиб. диапазона",
    [ROLLER_MENU_SERVICE_RESTART]    = "Сброс ошибок"
};

static const char* const head_enter_labels[] = {
    [ROLLER_ENTER_PASSWORD]     = "Введите пароль",
    [ROLLER_ENTER_TIME]         = "Время",
    [ROLLER_ENTER_DATA]      	= "Дата",
    [ROLLER_ENTER_DATATIME]  	= "Дата и время",
    [ROLLER_ENTER_MEAS]  		= "Ед. изм.",
	[ROLLER_ENTER_DISK]      	= "Дискр.",
    [ROLLER_ENTER_CONC]       	= "Концентр.",
    [ROLLER_ENTER_LIMIT1]  	 	= "Порог 1",
    [ROLLER_ENTER_LIMIT2]       = "Порог 2",
    [ROLLER_ENTER_LIMIT3] 		= "Порог 3",
};

static const char* const head_time_labels[] = {
    [ROLLER_TIMER_WARM]     	= "Прогрев",
    [ROLLER_TIMER_TURN_OFF]     = "Выключение",
    [ROLLER_TIMER_SOS]      	= "SOS",
};

static const char* const head_out_labels[] = {
    [ROLLER_HEAD_RANGE_GAS]     = "Подайте эталонный газ",
    [ROLLER_HEAD_ZERO_GAS]     	= "Подайте нулевой газ",
	[ROLLER_HEAD_CALIB_GAS]     = "Калибровать",
	[3]							= "Завершение калибровки датчика",
	[4]							= "Выполнено",
};

//------------------------------------------------------------------------------
const char * env_labels[2][3] = {
    { "Температура",  "Давление", "Влажность" },   // RU
    { "Temperature",  "Pressure", "Humidity"  }    // EN
};

const char * info_format[2] = {
    "%d°С, %d%%, %dmmHg", // RU
    "%d°С, %d%%, %dmmHg"    // EN
};

const char * time_calib_multi[2] = {
	"%dс.", // RU
	"%ds."  // EN
};

const char * num_channel[2] = {
	"K", // RU
	"C"  // EN
};

const char	Menu_unit[2][LCD_STR_UNIT_END][12] = {
		{
	{"PPM"},
	{"мг/м3"},
	{"%НКПР"},
	{"%ОБ.Д"},
		},
		{
	{"PPM"},
	{"mg/m3"},
	{"%LEL"},
	{"%VOL"},
		},
};

const char * pump_options[] = { "Насос Выкл.", "Насос Вкл." };

//===========================================================================================================================
// Массивы ключей переводов для ошибок (в Flash-памяти)
const char* const error_keys_chan[10] = {
    "Порог 1",            // 0 - ST_BIT_LIMIT1
    "Порог 2",            // 1 - ST_BIT_LIMIT2
    "Порог 3",            // 2 - ST_BIT_LIMIT3
    "Прев. STEL",         // 3 - ST_BIT_LIMIT_STEL
    "Прев. TWA",          // 4 - ST_BIT_LIMIT_TWA
    "Прев. диапазона",    // 5 - ST_BIT_EXCEEDED_THE_RANGE
    "Ошибка сенсора",     // 6 - ST_BIT_SENSOR_FAILED
    "Ош. калиб. нуля",    // 7 - ST_BIT_AUTO_ZERO_ERR
    "Ош. калиб. диапаз.", // 8 - ST_BIT_AUTO_SPAN_ERR
    "Время калибровки"    // 9 - ST_BIT_CALIB_INTERVAL
};

const char* const error_keys_sys[12] = {
    "Время калибровки",       // 0 - ST_COMMON_BIT_ERR_CALIB_INTERVAL
    "Время BUMP TEST",        // 1 - ST_COMMON_BIT_ERR_BUMP_INTERVAL
    "Низкий заряд АКБ",       // 2 - ST_COMMON_BIT_CRITICAL_LOW_BATTERY
    "Ошибка МЕМ",             // 3 - ST_COMMON_BIT_ERR_AT45
    "Ошибка ТЕМП",            // 4 - ST_COMMON_BIT_ERR_STS4
    "Ошибка BLE",             // 5 - ST_COMMON_BIT_ERR_BLE
    "Ошибка GPS",             // 6 - ST_COMMON_BIT_ERR_GPS
    "Ошибка GSM",             // 7 - ST_COMMON_BIT_ERR_GSM
    "Ошибка LORA",            // 8 - ST_COMMON_BIT_ERR_LORA
    "Ошибка LCD",             // 9 - ST_COMMON_BIT_ERR_LCD
    "Крит. разряд батареи",   // 10 - ST_COMMON_BIT_CRITICAL_BATTERY
    "Ошибка БАТ"              // 11 - ST_COMMON_BIT_ERR_MAX17
};

const char* const lora_status_keys[8] = {
    "Группа",             // 0 - ST_LORA_BIT_GROUP
    "Назад",              // 1 - ST_LORA_BIT_BACK_GROUP
    "Группа SOS",         // 2 - ST_LORA_BIT_SOS_GROUP
    "",                   // 3 - ST_LORA_BIT_ACTIV_MOD (не используется)
    "Зона",               // 4 - ST_LORA_BIT_ZONE
    "SOS",                // 5 - ST_LORA_BIT_SOS
    "",                   // 6 - ST_LORA_BIT_ECHO (не используется)
    ""                    // 7 - ST_LORA_BIT_CURRENT_C (не используется)
};

//===========================================================================================================================
// Очередь ошибок для отображения
error_queue_item_t error_queue[MAX_ERROR_QUEUE];
char current_error_text[40];
uint8_t error_queue_count = 0;
uint8_t error_display_index = 0;
uint32_t last_error_hash = 0;
//------------------------------------------------------------------------------
static uint8_t last_lang = 255; // Для отслеживания изменений
static uint16_t lcd_template = 0;
static lv_obj_t *my_screens = NULL;
static uint8_t pos_channel[COUNT_CHAN];
uint8_t active_channels_map[COUNT_CHAN];
uint8_t active_channels_count = 0;
uint8_t info_total_pages = 1;       	// Всего страниц (сделано extern в .h для доступа из меню)
ui_visibility_state_t ui_next_state;	// Глобальная или статическая переменная для хранения текущего заказа на отрисовку
//------------------------------------------------------------------------------
// Отображение батареи
static uint16_t seg_battery_charge_count = 0;
static uint16_t seg_battery_charge_flag = 0;
//------------------------------------------------------------------------------
static void LCD_Refresh_Language(uint8_t new_lang);
static void ui_set_label_text(lv_obj_t * obj, const char * text);
static void ui_set_obj_size(lv_obj_t * obj, int32_t w, int32_t h);
static void ui_set_obj_pos(lv_obj_t * obj, int32_t x, int32_t y);
static void ui_set_label_selection(lv_obj_t * obj, int32_t start, int32_t end);
static void ui_set_obj_align(lv_obj_t * obj, lv_align_t align);
static void ui_set_img_src(lv_obj_t * obj, const void * src);
static void ui_set_obj_bg_color(lv_obj_t * obj, lv_color_t color, lv_style_selector_t selector);
static void ui_set_obj_text_color(lv_obj_t * obj, lv_color_t color, lv_style_selector_t selector);
static void ui_set_obj_bg_opa(lv_obj_t * obj, lv_opa_t opa, lv_style_selector_t selector);
static void ui_set_obj_font(lv_obj_t * obj, const lv_font_t * font, lv_style_selector_t selector);
static void ui_set_obj_width(lv_obj_t * obj, int32_t w);
static void ui_set_bar_value(lv_obj_t * obj, int32_t val, lv_anim_enable_t anim);
static void ui_set_label_long_mode(lv_obj_t * obj, lv_label_long_mode_t mode);
static void ui_set_obj_image_recolor(lv_obj_t * obj, lv_color_t color, lv_style_selector_t selector);
static void ui_set_obj_image_recolor_opa(lv_obj_t * obj, lv_opa_t opa, lv_style_selector_t selector);
//------------------------------------------------------------------------------
static void ui_update_input_panel_head_out(SNS_CFG_Type *pSnsCfg, uint32_t t1, bool show_roller, int8_t rank, uint8_t ok_idx, uint8_t back_idx, uint16_t enter);
static void ui_update_input_panel_head_out_text(bool show_roller, uint16_t enter);
static void ui_update_input_panel_head_out_calib(SNS_CFG_Type *pSnsCfg, uint32_t t1, uint8_t state, int8_t rank, uint8_t ok_idx, uint8_t back_idx, uint16_t enter);
static void ui_update_input_panel_head_out_button(uint16_t enter);
static void ui_menu_item_set_focus(lv_obj_t * obj_text, lv_obj_t * obj_panel, bool is_selected, bool is_transparent);
static void ui_menu_item_set_recolor(lv_obj_t * obj_img, bool is_recolor_on);
static void ui_update_input_panel_head_enter(const char * text, int8_t rank, int8_t blink_idx, uint8_t ok_idx, uint8_t back_idx, uint16_t enter);
static void LCD_DisplayUnitMeasure(SNS_CFG_Type *pSnsCfg, uint16_t num, lv_obj_t * lbl_measure);
static void LCD_Fill_Info_Page(lv_obj_t *left_obj, lv_obj_t *right_obj, const char **labels, const char **values, uint8_t start_idx, uint8_t count_on_page);
//===========================================================================================================================
// Статические вспомогательные функции (не видны извне)
static void LCD_ClearErrorQueue(void);
static void LCD_FormatErrorText(char *out_buf, uint8_t buf_size, const error_queue_item_t *item, SNS_CFG *pSnsCfg);
static uint32_t LCD_CalculateErrorHash(SNS_CFG *pSnsCfg);
//===========================================================================================================================
static void ui_menu_item_set_focus(lv_obj_t * obj_text, lv_obj_t * obj_panel, bool is_selected, bool is_transparent)
{
    if (obj_panel == NULL) return;

    // Состояния-флаги:
    // LV_STATE_USER_1 — флаг "инициализация пройдена"
    // LV_STATE_CHECKED — флаг "был выбран в прошлый раз"

    bool is_initialized = lv_obj_has_state(obj_panel, LV_STATE_USER_1);
    bool last_selected  = lv_obj_has_state(obj_panel, LV_STATE_CHECKED);

    // Если уже инициализировано И состояние выбора не изменилось — ВЫХОДИМ
    if (is_initialized && (last_selected == is_selected)) {
        // Исключение для роллера (если нужно обновлять его чаще)
        if (obj_text == NULL || !lv_obj_check_type(obj_text, &lv_roller_class)) {
            return;
        }
    }

    // --- ОБНОВЛЯЕМ СОСТОЯНИЕ ---

    // Помечаем, что инициализация выполнена
    if (!is_initialized) lv_obj_add_state(obj_panel, LV_STATE_USER_1);

    // Запоминаем текущий выбор
    if (is_selected) lv_obj_add_state(obj_panel, LV_STATE_CHECKED);
    else lv_obj_clear_state(obj_panel, LV_STATE_CHECKED);

    uint8_t color_idx = is_selected ? 0 : 1;
    lv_color_t text_col  = lv_color_hex(color_text[color_idx]);
    lv_color_t bg_col   = lv_color_hex(color_background[color_idx]);

    lv_obj_remove_local_style_prop(obj_panel, LV_STYLE_BG_COLOR, LV_PART_MAIN);
	lv_obj_remove_local_style_prop(obj_panel, LV_STYLE_BG_OPA, LV_PART_MAIN);
	lv_obj_remove_local_style_prop(obj_panel, LV_STYLE_BG_GRAD_DIR, LV_PART_MAIN);

    // Красим текст/выбранную часть роллера
	if(obj_text != NULL) {
		if(lv_obj_check_type(obj_text, &lv_roller_class)) {
			// РОЛЛЕР: красим только выделенную часть
			ui_set_obj_text_color(obj_text, text_col , LV_PART_SELECTED);
		}
		else {
			// ЛЕЙБЛ: красим основную часть (LV_PART_MAIN)
			ui_set_obj_text_color(obj_text, text_col , LV_PART_MAIN);
		}
	}
    // Красим фон панели
//    ui_set_obj_bg_color(obj_panel, lv_color_hex(color_background[color_idx]), LV_PART_MAIN | LV_STATE_DEFAULT);

    // 2. Логика горизонтального градиента
    if(is_selected) {
        // Начальный цвет (слева) - ваш основной синий 0x215CA2
    	ui_set_obj_bg_opa(obj_panel, LV_OPA_COVER, LV_PART_MAIN);
    	// Конечный цвет (справа) - оттенок синего (делаем чуть светлее)
    	ui_set_obj_bg_color(obj_panel, lv_color_hex(0x347CD0), LV_PART_MAIN);

    	lv_obj_set_style_bg_grad_color(obj_panel, bg_col, LV_PART_MAIN);
        // Направление: Горизонтальное (слева направо)
        lv_obj_set_style_bg_grad_dir(obj_panel, LV_GRAD_DIR_HOR, LV_PART_MAIN);
        // Настройка точки перехода (50%):
        // LVGL использует шкалу 0-255. 128 - это ровно 50% ширины.
        lv_obj_set_style_bg_main_stop(obj_panel, 0, LV_PART_MAIN);     // Начало основного цвета
        lv_obj_set_style_bg_grad_stop(obj_panel, 255, LV_PART_MAIN);   // Конец перехода на 50% ширины
    }
    else {
    	lv_obj_set_style_bg_grad_dir(obj_panel, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    	ui_set_obj_bg_color(obj_panel, bg_col, LV_PART_MAIN);
    	ui_set_obj_bg_opa(obj_panel, is_transparent ? LV_OPA_TRANSP : LV_OPA_COVER, LV_PART_MAIN);
    }
}

static void ui_menu_item_set_recolor(lv_obj_t * obj_img, bool is_recolor_on)
{
    if (obj_img == NULL) return;

    if (!lv_obj_check_type(obj_img, &lv_image_class)) return;
    lv_color_t text_col  = lv_color_hex(color_text[0]);
    if (is_recolor_on) {
        // Включаем перекрашивание: задаем цвет и полную заливку этим цветом
    	ui_set_obj_image_recolor(obj_img, text_col, LV_PART_MAIN);
    	ui_set_obj_image_recolor_opa(obj_img, LV_OPA_COVER, LV_PART_MAIN);
    } else {
        // Выключаем перекрашивание: сбрасываем интенсивность в 0 (прозрачно)
    	ui_set_obj_image_recolor_opa(obj_img, LV_OPA_TRANSP, LV_PART_MAIN);
    }
}

//===========================================================================================================================
static void ui_update_input_panel_head_enter(const char * text, int8_t rank, int8_t blink_idx, uint8_t ok_idx, uint8_t back_idx, uint16_t enter)
{
    // 1. Установка текста в лейбл
    ui_set_label_text(ui_LabelSetEnter, text);

    // 2. Управление эффектом мигания (Selection)
    if (blink_idx >= 0) {
    	ui_set_label_selection(ui_LabelSetEnter, blink_idx, blink_idx + 1);

        // Стили выделения (белый текст на прозрачном фоне)
    	ui_set_obj_text_color(ui_LabelSetEnter, lv_color_hex(color_text[0]), LV_PART_SELECTED);
        ui_set_obj_bg_color(ui_LabelSetEnter, lv_color_hex(color_background[0]), LV_PART_SELECTED);
        ui_set_obj_bg_opa(ui_LabelSetEnter, LV_OPA_TRANSP, LV_PART_SELECTED);
    } else {
    	if((enter == ROLLER_ENTER_MEAS) || (enter == ROLLER_ENTER_DISK)){
    		ui_menu_item_set_focus(ui_LabelSetEnter, ui_LabelSetEnter, (rank == 0), false);
    	}
    	else{
			// Выключаем выделение, если индекс отрицательный
    		ui_set_label_selection(ui_LabelSetEnter, LV_LABEL_TEXT_SELECTION_OFF, LV_LABEL_TEXT_SELECTION_OFF);
    	}
    }

    // 3. Подсветка кнопок управления
    // Состояние 0 - активно (инверсия), 1 - обычно
    uint8_t ok_st   = (rank == ok_idx)   ? 1 : 0;
    uint8_t back_st = (rank == back_idx) ? 1 : 0;

    ui_menu_item_set_focus(ui_LabelOk, ui_LabelOk, ok_st, false);
    ui_menu_item_set_focus(ui_LabelBack, ui_LabelBack, back_st, false);

    // 4. Глобальные настройки панели
    ui_set_label_text(ui_LabelHeadEnter, _(head_enter_labels[enter])); // или ваш источник текста

    ui_next_state.head_enter = false;
}

//===========================================================================================================================
static void ui_update_input_panel_head_out(SNS_CFG_Type *pSnsCfg, uint32_t t1, bool show_roller, int8_t rank, uint8_t ok_idx, uint8_t back_idx, uint16_t enter)
{
    // 1. Управление видимостью виджетов (заменяет блоки if из out_1, out_2, out_3)
    // Если текст передан (не NULL) — показываем, если NULL — скрываем
    ui_set_obj_hidden(ui_DisplayChannelHeadOut, (t1 == 0));
    ui_set_obj_hidden(ui_LabelStopwatchOut,     (t1 == 0));
    ui_set_obj_hidden(ui_LabelOkOut,     		false);
    ui_set_obj_hidden(ui_LabelBackOut,     		false);
    ui_set_obj_hidden(ui_LabelHeadOut,         !show_roller);
    // Роллер
    if(show_roller) {
    	ui_set_label_text(ui_LabelHeadOut, _(head_out_labels[enter]));
    }

    // 2. Установка текстов (только если они переданы)
	//-------------------------------


	//-------------------------------
    if(t1){
    	if(pSnsCfg != NULL){
			uint8_t chan = pSnsCfg->channel;
			uint8_t disk = (pSnsCfg->DeviceSetting&MEASURE_DISK)>>OFFSET_DISK;
			float conversion = Get_Conversion_factor(chan);
			float data = (pSnsCfg->ConcentrationVal*conversion) + 0.4 / pow(10, disk);
			//-------------------------------
			sprintf(str_lcd, "%.*f", disk, data);
			ui_set_label_text(ui_comp_get_child(ui_DisplayChannelHeadOut, UI_COMP_DISPLAYCHANNEL_LABELCONCENTRATION), str_lcd);
			//-------------------------------
			lv_obj_t * lbl_measure = ui_comp_get_child(ui_DisplayChannelHeadOut, UI_COMP_DISPLAYCHANNEL_LABELMEASURE);
			LCD_DisplayUnitMeasure(pSnsCfg, DISP, lbl_measure);
			//-------------------------------
			sprintf(str_lcd, "%s", pSnsCfg->MolecularFormula);
			ui_set_label_text(ui_comp_get_child(ui_DisplayChannelHeadOut, UI_COMP_DISPLAYCHANNEL_LABELFORMULA), str_lcd);
			//-------------------------------
    	}
    }
    if(t1){
    	sprintf(str_lcd, time_calib_multi[APPLANG], t1/1000);
    	ui_set_label_text(ui_LabelStopwatchOut, str_lcd);
    }

    // 3. Подсветка кнопок (вынес в общую логику)
    uint8_t ok_st   = (rank == ok_idx)   ? 1 : 0;
    uint8_t back_st = (rank == back_idx) ? 1 : 0;

    ui_menu_item_set_focus(ui_LabelOkOut, ui_LabelOkOut, ok_st, false);
    ui_menu_item_set_focus(ui_LabelBackOut, ui_LabelBackOut, back_st, false);

    // 4. Глобальные настройки
    ui_update_input_panel_head_out_button(enter);

    // Показываем саму панель
    ui_next_state.head_out = false;
}

//===========================================================================================================================
static void ui_update_input_panel_head_out_text(bool show_roller, uint16_t enter)
{
    // 1. Управление видимостью виджетов
    // Если текст передан (не NULL) — показываем, если NULL — скрываем
    ui_set_obj_hidden(ui_DisplayChannelHeadOut, true);
    ui_set_obj_hidden(ui_LabelStopwatchOut,     true);
    ui_set_obj_hidden(ui_LabelOkOut,     		true);
    ui_set_obj_hidden(ui_LabelBackOut,     		true);
    ui_set_obj_hidden(ui_LabelHeadOut,         !show_roller);
    // Роллер
    if(show_roller) {
    	ui_set_label_text(ui_LabelHeadOut, _(head_out_labels[enter]));
    }

    // Показываем саму панель
    ui_next_state.head_out = false;
}

//===========================================================================================================================
static void ui_update_input_panel_head_out_calib(SNS_CFG_Type *pSnsCfg, uint32_t t1, uint8_t state, int8_t rank, uint8_t ok_idx, uint8_t back_idx, uint16_t enter)
{
    // 1. Управление видимостью виджетов (заменяет блоки if из out_1, out_2, out_3)
    // Если текст передан (не NULL) — показываем, если NULL — скрываем
    ui_set_obj_hidden(ui_DisplayChannelHeadOut, (t1 == 0));
    ui_set_obj_hidden(ui_LabelStopwatchOut,     (t1 == 0));
    ui_set_obj_hidden(ui_LabelOkOut,     		false);
    ui_set_obj_hidden(ui_LabelBackOut,     		false);
    ui_set_obj_hidden(ui_LabelHeadOut,         	true);

	//-------------------------------
    if(t1){
    	if(pSnsCfg != NULL){
			uint8_t chan = pSnsCfg->channel;
			uint8_t disk = (pSnsCfg->DeviceSetting&MEASURE_DISK)>>OFFSET_DISK;
			float conversion = Get_Conversion_factor(chan);
			float data = (pSnsCfg->ConcentrationVal*conversion) + 0.4 / pow(10, disk);
			//-------------------------------
			sprintf(str_lcd, "%.*f", disk, data);
			ui_set_label_text(ui_comp_get_child(ui_DisplayChannelHeadOut, UI_COMP_DISPLAYCHANNEL_LABELCONCENTRATION), str_lcd);
			//-------------------------------
			lv_obj_t * lbl_measure = ui_comp_get_child(ui_DisplayChannelHeadOut, UI_COMP_DISPLAYCHANNEL_LABELMEASURE);
			if(t1/2000){
			LCD_DisplayUnitMeasure(pSnsCfg, DISP, lbl_measure);
			}
			//-------------------------------
			sprintf(str_lcd, "%s", pSnsCfg->MolecularFormula);
			ui_set_label_text(ui_comp_get_child(ui_DisplayChannelHeadOut, UI_COMP_DISPLAYCHANNEL_LABELFORMULA), str_lcd);
			//-------------------------------
			if(state == 0){
				ui_set_obj_bg_color(ui_comp_get_child(ui_DisplayChannelHeadOut, UI_COMP_DISPLAYCHANNEL_DISPLAYCHANNEL), lv_color_hex(color_Chan[0]), LV_PART_MAIN | LV_STATE_DEFAULT);
			}
			else if(state == 1){
				ui_set_obj_bg_color(ui_comp_get_child(ui_DisplayChannelHeadOut, UI_COMP_DISPLAYCHANNEL_DISPLAYCHANNEL), lv_color_hex(color_Chan[2]), LV_PART_MAIN | LV_STATE_DEFAULT);
			}
			else if(state == 2){
				ui_set_obj_bg_color(ui_comp_get_child(ui_DisplayChannelHeadOut, UI_COMP_DISPLAYCHANNEL_DISPLAYCHANNEL), lv_color_hex(color_Chan[3]), LV_PART_MAIN | LV_STATE_DEFAULT);
			}
			else if(state == 3){
				ui_set_obj_bg_color(ui_comp_get_child(ui_DisplayChannelHeadOut, UI_COMP_DISPLAYCHANNEL_DISPLAYCHANNEL), lv_color_hex(color_Chan[0]), LV_PART_MAIN | LV_STATE_DEFAULT);
			}
    	}
    }
    if(t1){
    	if((state == 0) || (state == 3)){
    		sprintf(str_lcd, time_calib_multi[APPLANG], t1/1000);
    	}
    	else if(state == 1){
    		sprintf(str_lcd, _("Ошибка"));
    	}
    	else if(state == 2){
    		sprintf(str_lcd, _("Ок"));
    	}
//    	ui_set_label_text(ui_LabelInfoStr, str_lcd);

//    	sprintf(str_lcd, time_calib_multi[APPLANG], t1/1000);
    	ui_set_label_text(ui_LabelStopwatchOut, str_lcd);
    }

    // 3. Подсветка кнопок (вынес в общую логику)
    uint8_t ok_st   = (rank == ok_idx)   ? 1 : 0;
    uint8_t back_st = (rank == back_idx) ? 1 : 0;

    ui_menu_item_set_focus(ui_LabelOkOut, ui_LabelOkOut, ok_st, false);
    ui_menu_item_set_focus(ui_LabelBackOut, ui_LabelBackOut, back_st, false);

    // 4. Глобальные настройки
    ui_update_input_panel_head_out_button(enter);

    // Показываем саму панель
    ui_next_state.head_out = false;
}

static void ui_update_input_panel_head_out_button(uint16_t enter)
{
    // Защита от выхода за пределы массива
    if (enter >= ROLLER_HEAD_END) return;

    const ui_button_cfg_t *cfg = &btn_table[enter];

    // 1. Текст с переводом
    ui_set_label_text(ui_LabelOkOut, _(cfg->ok_key));
    ui_set_label_text(ui_LabelBackOut, _(cfg->back_key));

    // 2. Геометрия
    ui_set_obj_size(ui_LabelOkOut, cfg->width, 44);
    ui_set_obj_size(ui_LabelBackOut, cfg->width, 44);

    // 3. Выравнивание и позиция
    ui_set_obj_align(ui_LabelOkOut, cfg->align_ok);
    ui_set_obj_align(ui_LabelBackOut, cfg->align_back);

    ui_set_obj_pos(ui_LabelOkOut, cfg->ok_x, cfg->ok_y);
    ui_set_obj_pos(ui_LabelBackOut, cfg->back_x, cfg->back_y);
}

//===========================================================================================================================
static void LCD_DisplayUnitMeasure(SNS_CFG_Type *pSnsCfg, uint16_t num, lv_obj_t * lbl_measure)
{
	uint8_t dev_set = 0;
	uint8_t chan = pSnsCfg->channel;

	if(!lbl_measure) return;

	if(!TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_LCD_UNIT)){
//		ui_set_obj_hidden(lbl_measure, true);
//		return;
	}

	if(TEST_STATUS_ERR_BIT(ST_BIT_SENSOR_FAILED, chan)){
		ui_set_obj_hidden(lbl_measure, true);
		return;
	}

	if(num == BASIC){
		dev_set = (pSnsCfg->DeviceSetting&MEASURE_MASK_BASIC)>>MEASURE_OFFSET_BASIC;
	}
	else if(num == DISP){
		dev_set = pSnsCfg->DeviceSetting&MEASURE_MASK;
	}

	if(dev_set < LCD_STR_UNIT_END) {
		ui_set_obj_hidden(lbl_measure, false);
        // Самый быстрый способ: передаем указатель из Flash-памяти в LVGL без копирования
        ui_set_label_text(lbl_measure, Menu_unit[APPLANG][dev_set]);
	}
	else{
		ui_set_obj_hidden(lbl_measure, true);
	}
}

//===========================================================================================================================
// Заполнение двух колонок:
// left_lbl  -> ВСЕ названия текущей страницы (с start_idx по start_idx + count_on_page)
// right_lbl -> ВСЕ значения текущей страницы (с start_idx по start_idx + count_on_page)
// Строки синхронизированы: i-я строка слева соответствует i-й строке справа.
//===========================================================================================================================
static char buf_left[800];  // Буфер побольше, чтобы влезло 9 длинных названий
static char buf_right[800]; // Буфер побольше для значений
static void LCD_Fill_Info_Page(lv_obj_t *left_lbl, lv_obj_t *right_lbl,
                               const char **labels, const char **values,
                               uint8_t start_idx, uint8_t count_on_page)
{
    if (!left_lbl || !right_lbl || count_on_page == 0) {
        ui_set_label_text(left_lbl, "");
        ui_set_label_text(right_lbl, "");
        return;
    }


    uint16_t pos_l = 0;
    uint16_t pos_r = 0;

    // Проходим по ВСЕМ строкам текущей страницы (от 0 до count_on_page)
    for (uint8_t i = 0; i < count_on_page; i++) {
        uint8_t idx = start_idx + i; // Глобальный индекс в массиве

        // Добавляем перенос строки перед каждой строкой, кроме первой
        if (i > 0) {
            buf_left[pos_l++] = '\n';
            buf_right[pos_r++] = '\n';
        }

        // Копируем название в левый буфер
        int written_l = snprintf(&buf_left[pos_l], sizeof(buf_left) - pos_l, "%s", labels[idx]);
        if (written_l > 0) pos_l += written_l;

        // Копируем значение в правый буфер
        int written_r = snprintf(&buf_right[pos_r], sizeof(buf_right) - pos_r, "%s", values[idx]);
        if (written_r > 0) pos_r += written_r;
    }

    // Завершаем строки нулем
    buf_left[pos_l] = '\0';
    buf_right[pos_r] = '\0';

    // Устанавливаем текст в виджеты
    ui_set_label_text(left_lbl, buf_left);
    ui_set_label_text(right_lbl, buf_right);

    ui_set_obj_hidden(left_lbl, false);
    ui_set_obj_hidden(right_lbl, false);
}

//===========================================================================================================================
// Очистка очереди ошибок
static void LCD_ClearErrorQueue(void)
{
    memset(error_queue, 0, sizeof(error_queue));
    error_queue_count = 0;
    error_display_index = 0;
}

//===========================================================================================================================
// Генерация текста ошибки по метаданным (используем _() для перевода)
static void LCD_FormatErrorText(char *out_buf, uint8_t buf_size,
                                const error_queue_item_t *item, SNS_CFG *pSnsCfg)
{
    out_buf[0] = '\0';

    if (item->category == 0) {
        // Ошибка канала: "Порог 1, CO2"
        if ((item->channel < COUNT_CHAN) && (item->error_bit < 10)) {
            snprintf(out_buf, buf_size, "%s, %s",
                    _(error_keys_chan[item->error_bit]),
                    pSnsCfg->Sensor[item->channel].MolecularFormula);
        }
    }
    else if (item->category == 1) {
        // Системная ошибка
        if (item->error_bit < 12) {
            snprintf(out_buf, buf_size, "%s", _(error_keys_sys[item->error_bit]));
        }
    }
    else if (item->category == 2) {
        // LORA статус
        if ((item->error_bit < 8) && (lora_status_keys[item->error_bit][0] != '\0')) {
            snprintf(out_buf, buf_size, "LORA: %s", _(lora_status_keys[item->error_bit]));
        }
    }
}

//===========================================================================================================================
// Хэш-функция для отслеживания изменений в ошибках
static uint32_t LCD_CalculateErrorHash(SNS_CFG *pSnsCfg)
{
    // Статические переменные хранят предыдущее состояние между вызовами
    static uint32_t prev_chan_err[COUNT_CHAN] = {0};
    static uint32_t prev_sys_err = 0;
    static uint8_t prev_lora = 0;

    uint8_t changed = 0;

    // 1. Проверяем каждый канал отдельно
    for(uint8_t ch = 0; ch < COUNT_CHAN; ch++) {
        uint32_t curr = pSnsCfg->Sensor[ch].StateErr & ERROR_MASK_ST_1;

        // Сравниваем с предыдущим значением ЭТОГО канала
        if(curr != prev_chan_err[ch]) {
            changed = 1;
            prev_chan_err[ch] = curr;  // Обновляем сохранённое значение
        }
    }

    // 2. Проверяем системные ошибки
    uint32_t curr_sys = pSnsCfg->Config_common.StateErr & ERROR_MASK_ST_2;
    if(curr_sys != prev_sys_err) {
        changed = 1;
        prev_sys_err = curr_sys;
    }

    // 3. Проверяем LORA
    #if CONFIG_LORA
    uint8_t curr_lora = Lora_GetControlLora() & 0xFF;
    if(curr_lora != prev_lora) {
        changed = 1;
        prev_lora = curr_lora;
    }
    #endif

    return changed;
}

//===========================================================================================================================
// Построение очереди ошибок
void LCD_BuildErrorQueue(SNS_CFG *pSnsCfg)
{
    LCD_ClearErrorQueue();

    // 1. Ошибки каналов
    for (uint8_t ch = 0; ch < COUNT_CHAN; ch++) {
        if (TESTBIT(pSnsCfg->Sensor[ch].State, ST_BIT_CHANNEL_TURN_OFF)) continue;

        uint32_t err_mask = pSnsCfg->Sensor[ch].StateErr & ERROR_MASK_ST_1;

        for (uint8_t bit = 0; bit < 10; bit++) {
            if ((TESTBIT(err_mask, bit)) && (error_queue_count < MAX_ERROR_QUEUE)) {
                error_queue[error_queue_count].channel = ch;
                error_queue[error_queue_count].category = 0;
                error_queue[error_queue_count].error_bit = bit;
                error_queue[error_queue_count].key = (0 << 8) | (ch << 4) | bit;
                error_queue_count++;
            }
        }
    }

    // 2. Системные ошибки
    uint32_t sys_err_mask = pSnsCfg->Config_common.StateErr & ERROR_MASK_ST_2;
    for (uint8_t bit = 0; bit < 12; bit++) {
        if ((TESTBIT(sys_err_mask, bit)) && (error_queue_count < MAX_ERROR_QUEUE)) {
            error_queue[error_queue_count].channel = 0xFF;
            error_queue[error_queue_count].category = 1;
            error_queue[error_queue_count].error_bit = bit;
            error_queue[error_queue_count].key = (1 << 8) | (0xFF << 4) | bit;
            error_queue_count++;
        }
    }

    // 3. Статусы LORA (только биты 0, 1, 2, 4, 5)
    #if CONFIG_LORA
    if (TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_TURN_ON_LORA)) {
        const uint8_t lora_bits[] = {0, 1, 2, 4, 5};

        for (uint8_t i = 0; i < 5; i++) {
            uint8_t bit = lora_bits[i];
            if ((TESTBIT(Lora_GetControlLora(), bit)) && (error_queue_count < MAX_ERROR_QUEUE)) {
                error_queue[error_queue_count].channel = 0xFE;
                error_queue[error_queue_count].category = 2;
                error_queue[error_queue_count].error_bit = bit;
                error_queue[error_queue_count].key = (2 << 8) | (0xFE << 4) | bit;
                error_queue_count++;
            }
        }
    }
    #endif
}

//===========================================================================================================================
// Обновление отображения ошибок
void LCD_UpdateErrorDisplay(SNS_CFG *pSnsCfg, uint8_t state_flashing)
{
	static uint8_t prev_state_flashing = 0;

	uint8_t changed = LCD_CalculateErrorHash(pSnsCfg);

    // Если ошибки изменились — пересобираем очередь, НО сохраняем текущий элемент
    if (changed) {
        // 1. Запоминаем ключ текущей ошибки ДО пересборки
        uint16_t saved_key = 0;
        if ((error_queue_count > 0) && (error_display_index < error_queue_count)) {
            saved_key = error_queue[error_display_index].key;
        }

        // 2. Пересобираем очередь
        LCD_BuildErrorQueue(pSnsCfg);

        // 3. Ищем сохранённый ключ в новой очереди
        error_display_index = 0;  // По умолчанию — первая ошибка
        if (saved_key != 0) {
            for (uint8_t i = 0; i < error_queue_count; i++) {
                if (error_queue[i].key == saved_key) {
                    error_display_index = i;
                    break;
                }
            }
        }
    }

    if (error_queue_count > 0) {
        if ((state_flashing != prev_state_flashing) &&
            (error_queue_count > 1)) {

            error_display_index++;
            if (error_display_index >= error_queue_count) {
                error_display_index = 0;
            }
        }
        // Сохраняем состояние для следующего вызова
        prev_state_flashing = state_flashing;

        if (error_display_index < error_queue_count) {
            LCD_FormatErrorText(current_error_text, sizeof(current_error_text),
                               &error_queue[error_display_index], pSnsCfg);

            // 1. Получаем ширину виджета
            lv_coord_t label_width = lv_obj_get_width(ui_LabelInfoStr);

            // 2. Вычисляем ширину текста в пикселях
            lv_point_t txt_size;
            lv_txt_get_size(&txt_size, current_error_text,
                           lv_obj_get_style_text_font(ui_LabelInfoStr, LV_PART_MAIN),
                           0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

            // Выбираем режим в зависимости от длины текста
            lv_label_long_mode_t needed_mode = (txt_size.x > label_width) ?
                                               LV_LABEL_LONG_SCROLL : LV_LABEL_LONG_CLIP;

            // Устанавливаем режим ТОЛЬКО если он изменился
            ui_set_label_long_mode(ui_LabelInfoStr, needed_mode);

            ui_set_label_text(ui_LabelInfoStr, current_error_text);
            ui_next_state.info_str = false;
        }
    }
    else {
    	lv_label_set_long_mode(ui_LabelInfoStr, LV_LABEL_LONG_CLIP);
        ui_next_state.info_str = true;
    }
}

//===========================================================================================================================
void my_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
//	while(GET_LCD_SPI_PORT_State() == 1);

	int height = area->y2 - area->y1 + 1;
    int width = area->x2 - area->x1 + 1;

    TFT_FlushBuffer(area->x1, area->y1, width, height, px_map);

//    /* IMPORTANT!!!
//     * Inform the graphics library that you are ready with the flushing */
//    lv_display_flush_ready(disp);
}

//===========================================================================================================================
void ui_lv_init(void)
{
	lv_init();
	disp = lv_display_create(TFT_WIDTH, TFT_HEIGHT); /* Basic initialization with horizontal and vertical resolution in pixels */
	lv_display_set_flush_cb(disp, my_flush_cb); /* Set a flush callback to draw to the display */
	uint32_t buf_size = BUF_SIZE * lv_color_format_get_size(lv_display_get_color_format(disp));
//	  buf1 = lv_malloc(buf_size);
//	  buf2 = lv_malloc(buf_size);
	lv_display_set_buffers(disp, buf_lcd, buf_lcd2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL); /* Set an initialized buffer */
	// 1. Инициализируем пакет переводов
	lv_i18n_init(lv_i18n_language_pack);

	// 2. Устанавливаем нужный язык (как в вашем .yml файле)
	lv_i18n_set_locale("ru-RU");

	ui_init();

	ui_ScreenHeadMain_screen_init();
	lv_obj_set_parent(ui_ImageMirax, lv_layer_top());
	lv_obj_set_parent(ui_ImageBattery, lv_layer_top());
	lv_obj_set_parent(ui_LabelInfoStr, lv_layer_top());
	lv_obj_set_parent(ui_PanelHead, lv_layer_top());
	lv_obj_set_parent(ui_PanelHeadMenu, lv_layer_top());
	lv_obj_set_parent(ui_PanelHeadMenuUp, lv_layer_top());
	lv_obj_set_parent(ui_PanelHeadEnter, lv_layer_top());
	lv_obj_set_parent(ui_PanelHeadTimer, lv_layer_top());
	lv_obj_set_parent(ui_PanelHeadOut, lv_layer_top());
}

//===========================================================================================================================
static void LCD_Refresh_Language(uint8_t new_lang)
{
	if (new_lang != last_lang) {
		last_lang = new_lang;
		// 1. Устанавливаем новую локаль в библиотеке lv_i18n
		if(new_lang == RUS) {
			lv_i18n_set_locale("ru-RU");
		} else {
			lv_i18n_set_locale("en-GB");
		}

		ui_ScreenHeadMain_screen_relocalize();

	    // 2. Определяем, какой экран сейчас виден пользователю
		lv_obj_t * act_scr = lv_scr_act();

		// 3. Вызываем перерисовку статических текстов (из SLS)
		if(act_scr == ui_ScreenStart)             ui_ScreenStart_screen_relocalize();
		else if(act_scr == ui_ScreenMeasurements) ui_ScreenMeasurements_screen_relocalize();
		else if(act_scr == ui_ScreenMenu)         ui_ScreenMenu_screen_relocalize();
		else if(act_scr == ui_ScreenSetting)      ui_ScreenSetting_screen_relocalize();
		else if(act_scr == ui_ScreenSettingSetChannel) ui_ScreenSettingSetChannel_screen_relocalize();
		else if(act_scr == ui_ScreenSettingSet)   ui_ScreenSettingSet_screen_relocalize();
	}
}

//===========================================================================================================================
void ui_set_obj_hidden(lv_obj_t * obj, bool hide)
{
    if (obj == NULL) return;

    // Сначала проверяем: нужно ли вообще что-то менять?
    bool is_currently_hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);

    if (hide != is_currently_hidden) {
        // Вызываем только если состояние РЕАЛЬНО меняется
        if (hide) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ui_set_label_text(lv_obj_t * obj, const char * text)
{
    if (obj == NULL || text == NULL) return;

    // Сравниваем новый текст с тем, что уже записан в объекте
    if (strcmp(lv_label_get_text(obj), text) != 0) {
    	lv_label_set_text(obj, text);
    }
}

static void ui_set_obj_size(lv_obj_t * obj, int32_t w, int32_t h)
{
    if(obj == NULL) return;
    if(lv_obj_get_width(obj) != w || lv_obj_get_height(obj) != h) {
        lv_obj_set_size(obj, w, h);
    }
}

static void ui_set_obj_pos(lv_obj_t * obj, int32_t x, int32_t y)
{
    if(obj == NULL) return;
    if(lv_obj_get_x(obj) != x || lv_obj_get_y(obj) != y) {
        lv_obj_set_pos(obj, x, y);
    }
}

static void ui_set_label_selection(lv_obj_t * obj, int32_t start, int32_t end)
{
    if(obj == NULL) return;
    if(lv_label_get_text_selection_start(obj) != start || lv_label_get_text_selection_end(obj) != end) {
        lv_label_set_text_selection_start(obj, start);
        lv_label_set_text_selection_end(obj, end);
    }
}

static void ui_set_obj_align(lv_obj_t * obj, lv_align_t align)
{
    if(obj == NULL) return;
    // Проверяем текущее выравнивание через стили
    if(lv_obj_get_style_align(obj, LV_PART_MAIN) != align) {
        lv_obj_set_align(obj, align);
    }
}

static void ui_set_img_src(lv_obj_t * obj, const void * src)
{
    if(obj == NULL) return;
    // Сравниваем указатели на источник (картинку)
    if(lv_img_get_src(obj) != src) {
        lv_img_set_src(obj, src);
    }
}

static void ui_set_obj_bg_color(lv_obj_t * obj, lv_color_t color, lv_style_selector_t selector)
{
    if(obj == NULL) return;
    lv_color_t current_color = lv_obj_get_style_bg_color(obj, selector);
    if(lv_color_to_int(current_color) != lv_color_to_int(color)) {
        lv_obj_set_style_bg_color(obj, color, selector);
    }
}

static void ui_set_obj_text_color(lv_obj_t * obj, lv_color_t color, lv_style_selector_t selector)
{
    if(obj == NULL) return;
    lv_color_t current_color = lv_obj_get_style_text_color(obj, selector);
    if(lv_color_to_int(current_color) != lv_color_to_int(color)) {
        lv_obj_set_style_text_color(obj, color, selector);
    }
}

static void ui_set_obj_bg_opa(lv_obj_t * obj, lv_opa_t opa, lv_style_selector_t selector)
{
    if(obj == NULL) return;
    if(lv_obj_get_style_bg_opa(obj, selector) != opa) {
        lv_obj_set_style_bg_opa(obj, opa, selector);
    }
}

static void ui_set_obj_font(lv_obj_t * obj, const lv_font_t * font, lv_style_selector_t selector)
{
    if(obj == NULL || font == NULL) return;
    const lv_font_t * current_font = lv_obj_get_style_text_font(obj, selector);
    if(current_font != font) {
        lv_obj_set_style_text_font(obj, font, selector);
    }
}

static void ui_set_obj_width(lv_obj_t * obj, int32_t w)
{
    if(obj == NULL) return;
    if(lv_obj_get_width(obj) != w) {
        lv_obj_set_width(obj, w);
    }
}

static void ui_set_bar_value(lv_obj_t * obj, int32_t val, lv_anim_enable_t anim)
{
    if(obj == NULL) return;
    if(lv_bar_get_value(obj) != val) {
        lv_bar_set_value(obj, val, anim);
    }
}

static void ui_set_label_long_mode(lv_obj_t * obj, lv_label_long_mode_t mode)
{
    if(obj == NULL) return;
    if(lv_label_get_long_mode(obj) != mode) {
        lv_label_set_long_mode(obj, mode);
    }
}

static void ui_set_obj_image_recolor(lv_obj_t * obj, lv_color_t color, lv_style_selector_t selector)
{
    if(obj == NULL) return;
    lv_color_t current_color = lv_obj_get_style_image_recolor(obj, selector);
    // Если цвет изменился — записываем новый
    if(lv_color_to_int(current_color) != lv_color_to_int(color)) {
        lv_obj_set_style_image_recolor(obj, color, selector);
    }
}

static void ui_set_obj_image_recolor_opa(lv_obj_t * obj, lv_opa_t opa, lv_style_selector_t selector)
{
    if(obj == NULL) return;
    lv_opa_t current_opa = lv_obj_get_style_image_recolor_opa(obj, selector);
    // Если интенсивность изменилась — записываем новую
    if(current_opa != opa) {
        lv_obj_set_style_image_recolor_opa(obj, opa, selector);
    }
}

//===========================================================================================================================
void LCD_float_measure(SNS_CFG_Type *pSnsCfg, float data, uint8_t state_flashing, uint8_t num_channel)
{
	uint8_t chan = pSnsCfg->channel;
	uint8_t disk = (pSnsCfg->DeviceSetting&MEASURE_DISK)>>OFFSET_DISK;

    const channel_cfg_t *cfg = &lcd_templates[lcd_template][chan];

    // 2. Условие включения: канал включен в настройках И он активен в выбранном шаблоне
    bool should_be_visible = (!TESTBIT(pSnsCfg->State, ST_BIT_CHANNEL_TURN_OFF)) && cfg->active;

	if(should_be_visible){
		//-------------------------------
		ui_set_obj_bg_color(ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_DISPLAYCHANNEL), lv_color_hex(color_Chan[0]), LV_PART_MAIN | LV_STATE_DEFAULT);
		//-------------------------------
		lv_obj_t * lbl_measure = ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_LABELMEASURE);
		LCD_DisplayUnitMeasure(pSnsCfg, DISP, lbl_measure);
		//-------------------------------
		sprintf(str_lcd, "%s", pSnsCfg->MolecularFormula);
		ui_set_label_text(ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_LABELFORMULA), str_lcd);
		//-------------------------------
		if(!TESTBIT(pSnsCfg->StateErr, ST_BIT_SENSOR_FAILED)){
			if(TESTBIT(pSnsCfg->State, ST_BIT_WARM_SENSOR)){
				//-------------------------------
				float conversion = Get_Conversion_factor(chan);
				data = (data*conversion) + 0.4 / pow(10, disk);
				//-------------------------------
				sprintf(str_lcd, "%.*f", disk, data);
				ui_set_label_text(ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_LABELCONCENTRATION), str_lcd);
				//-------------------------------
				if(num_channel == NOCHAN){
					if(!((pSnsCfg->StateErr & ERROR_MASK_ST_1_1) && (state_flashing == 0))){
						state_flashing = 1;
					}
				}

				if(state_flashing == 0){
					ui_set_obj_bg_color(ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_DISPLAYCHANNEL), lv_color_hex(color_Chan[2]), LV_PART_MAIN | LV_STATE_DEFAULT);
				}

				if(TESTBIT(pSnsCfg->StateErr, ST_BIT_EXCEEDED_THE_RANGE)){
					ui_set_img_src(ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_IMAGEERR), &ui_img_imagechan_err_chan_png);
				}
				else if(TESTBIT(pSnsCfg->StateErr, ST_BIT_LIMIT3)){
					ui_set_img_src(ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_IMAGEERR), &ui_img_imagechan_lim3_chan_png);
				}
				else if(TESTBIT(pSnsCfg->StateErr, ST_BIT_LIMIT2)){
					ui_set_img_src(ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_IMAGEERR), &ui_img_imagechan_lim2_chan_png);
				}
				else if(TESTBIT(pSnsCfg->StateErr, ST_BIT_LIMIT1)){
					ui_set_img_src(ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_IMAGEERR), &ui_img_imagechan_lim1_chan_png);
				}
				else if(pSnsCfg->StateErr & ERROR_MASK_ST_1_1){
					ui_set_img_src(ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_IMAGEERR), &ui_img_imagechan_err_chan_png);
				}
				//-------------------------------
			}
			else{
				ui_set_label_text(ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_LABELCONCENTRATION), _("Прогрев"));
			}
		}
		else{
			ui_set_img_src(ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_IMAGEERR), &ui_img_imagechan_err_chan_png);
			ui_set_obj_bg_color(ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_DISPLAYCHANNEL), lv_color_hex(color_Chan[1]), LV_PART_MAIN | LV_STATE_DEFAULT);
			sprintf(str_lcd, "E-%01X6", chan);
			ui_set_label_text(ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_LABELCONCENTRATION), str_lcd);
		}
		//-------------------------------
		ui_set_obj_hidden(DisplayChannel[chan], false);
	}
	else{
		ui_set_obj_hidden(DisplayChannel[chan], true);
	}
}

//===========================================================================================================================
void LCD_float_chan_calib(SNS_CFG_Type *pSnsCfg, float data, CALIB_CFG *calib, uint8_t state)
{
	uint8_t chan = pSnsCfg->channel;
	uint8_t disk = (pSnsCfg->DeviceSetting&MEASURE_DISK)>>OFFSET_DISK;

	const channel_cfg_t *cfg = &lcd_templates[lcd_template][chan];

	bool should_be_visible = (!TESTBIT(pSnsCfg->State, ST_BIT_CHANNEL_TURN_OFF)) && cfg->active && TESTBIT(calib->calib_flag_disp, chan);

	if(should_be_visible){
		//-------------------------------
		if(calib->calib_flag_start){
			ui_set_label_text(ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_LABELCONCENTRATION), _("Ждите"));
		}
		else{
			float conversion = Get_Conversion_factor(chan);
			data = (data*conversion) + 0.4 / pow(10, disk);
			//-------------------------------
			sprintf(str_lcd, "%.*f", disk, data);
			ui_set_label_text(ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_LABELCONCENTRATION), str_lcd);
		}
		//-------------------------------
		lv_obj_t * lbl_measure = ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_LABELMEASURE);
		LCD_DisplayUnitMeasure(pSnsCfg, DISP, lbl_measure);
		//-------------------------------
		sprintf(str_lcd, "%s", pSnsCfg->MolecularFormula);
		ui_set_label_text(ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_LABELFORMULA), str_lcd);
		//-------------------------------
		if(state == 0){
			ui_set_obj_bg_color(ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_DISPLAYCHANNEL), lv_color_hex(color_Chan[0]), LV_PART_MAIN | LV_STATE_DEFAULT);
		}
		else if(state == 1){
			ui_set_obj_bg_color(ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_DISPLAYCHANNEL), lv_color_hex(color_Chan[2]), LV_PART_MAIN | LV_STATE_DEFAULT);
		}
		else if(state == 2){
			ui_set_obj_bg_color(ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_DISPLAYCHANNEL), lv_color_hex(color_Chan[3]), LV_PART_MAIN | LV_STATE_DEFAULT);
		}
		else if(state == 3){
			ui_set_obj_bg_color(ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_DISPLAYCHANNEL), lv_color_hex(color_Chan[0]), LV_PART_MAIN | LV_STATE_DEFAULT);
		}
		//-------------------------------
		ui_set_obj_hidden(ui_comp_get_child(DisplayChannel[chan], UI_COMP_DISPLAYCHANNEL_IMAGEERR), true);
		ui_set_obj_hidden(DisplayChannel[chan], false);
	}
	else{
		ui_set_obj_hidden(DisplayChannel[chan], true);
	}
}

//===========================================================================================================================
void LCD_PosChannel_init(SNS_CFG *pSnsCfg)
{
	uint8_t state_flag_err = 0;
	uint16_t count_chan = 0;
	uint16_t count_chan_max = 0;
#if (DEVICE_NUMBER == Device3_Pro)
	count_chan_max = 7;
#else
	count_chan_max = 4;
#endif
	memset(pos_channel, NOCHAN, sizeof(pos_channel));
	for(uint8_t chan_r = CHAN0; chan_r < COUNT_CHAN; chan_r++){
		if( pSnsCfg->Sensor[chan_r].channel == chan_r){
			pos_channel[count_chan] = chan_r;
			count_chan++;
			if((pSnsCfg->Sensor[chan_r].StateErr & ERROR_MASK_ST_1)||
					(pSnsCfg->Config_common.StateErr & ERROR_MASK_ST_2)){
				state_flag_err = 1;
			}
		}
	}

	if(count_chan >= count_chan_max){
		if(state_flag_err == 0){
			lcd_template = 0;
		}
		else{
			lcd_template = 2;
		}
	}
	else{
		if(state_flag_err == 0){
			lcd_template = 1;
		}
		else{
			lcd_template = 3;
		}
	}
}

//===========================================================================================================================
uint8_t LCD_GetActiveChannelsCount(SNS_CFG *pSnsCfg)
{
	uint8_t count_chan = 0;
	memset(active_channels_map, NOCHAN, sizeof(active_channels_map));
	for(uint8_t chan_r = CHAN0; chan_r < COUNT_CHAN; chan_r++){
		if( pSnsCfg->Sensor[chan_r].channel == chan_r){
			active_channels_map[count_chan] = chan_r;
			count_chan++;
		}
	}
	active_channels_count = count_chan;
    return count_chan;
}

//===========================================================================================================================
void LCD_init_DisplayChannelScreenMeasurements(SNS_CFG *pSnsCfg, uint8_t tid)
{
    if(tid >= 4) return;
    if(ui_DisplayChannel0 == NULL) return;

#if (CONFIG_MODEL_LCD == 0)
    lv_obj_t * ui_objs[8] = {
        ui_DisplayChannel0, ui_DisplayChannel1, ui_DisplayChannel2, ui_DisplayChannel3,
        ui_DisplayChannel4, ui_DisplayChannel5, ui_DisplayChannel6, ui_DisplayChannel7
    };
#elif (CONFIG_MODEL_LCD == 1)
    lv_obj_t * ui_objs[4] = {
        ui_DisplayChannel0, ui_DisplayChannel1, ui_DisplayChannel2, ui_DisplayChannel3
    };
#endif

    for(int i = 0; i < COUNT_CHAN; i++) {
        DisplayChannel[i] = ui_objs[i];
        const channel_cfg_t *cfg = &lcd_templates[tid][i];
        lv_obj_t *obj = ui_objs[i];

        // Только настраиваем геометрию и шрифты
        ui_set_obj_size(obj, cfg->w, cfg->h);
        ui_set_obj_pos(obj, cfg->x, cfg->y);

        lv_obj_t *lbl_conc = ui_comp_get_child(obj, UI_COMP_DISPLAYCHANNEL_LABELCONCENTRATION);
        if(lbl_conc) ui_set_obj_font(lbl_conc, cfg->font, 0);

        ui_set_obj_hidden(ui_comp_get_child(obj, UI_COMP_DISPLAYCHANNEL_IMAGEERR), false);
        // ВАЖНО: Если в шаблоне канал помечен как !active,
        // мы его принудительно скрываем здесь, чтобы LCD_float_measure его не включила
        if(!cfg->active) {
        	ui_set_obj_hidden(obj, true);
        }
    }
}

//===========================================================================================================================
uint16_t LCD_Get_Template(void)
{
	return lcd_template;
}

//===========================================================================================================================
uint8_t state_rotation = 0;
uint8_t state_rotation_old = 0;
uint16_t LCD_Fill_Template(SNS_CFG *pSnsCfg)
{
	static uint16_t last_tid = 255;
	uint16_t state_screen_change = 0;
	bool screen_was_changed = false;
	uint8_t current_state = IS_STATE_SCREEN();

#if (CONFIG_LIS3DH)
    if (lis3dh_orientation_changed()) {  // Ориентация изменилась?
    	state_rotation = lis3dh_get_rotation_state();
    }
#endif
	if(state_rotation != state_rotation_old){
		TFT_Rotation(state_rotation);
//		lv_obj_invalidate(lv_scr_act());
		state_rotation_old = state_rotation;
	}

	LCD_Image_SET(0);


	LCD_Refresh_Language(APPLANG);

	//-------------------------------
	// 1. Скрытие панелей
	ui_reset_visibility_flags();
	//-------------------------------
	// 2. Инфо-панель
#if TEST_GPS
	sprintf(str_lcd, "GPS %.5f , %.5f",	Latitude, Longitude);
#else
	sprintf(str_lcd, info_format[APPLANG],
			(int32_t)Sns_Cfg_struct.Config_common.Temperature,
			(int32_t)Sns_Cfg_struct.Config_common.Humidity,
			(int32_t)Sns_Cfg_struct.Config_common.Pressure);
//	float x,y,z;
//	lis3dh_get_cached_accel_g(&x,&y,&z);
//		sprintf(str_lcd,"%.2f %.2f %.2f", x, y, z);
#endif
#if (DEVICE_NUMBER == Device3_Pro)
	ui_set_label_text(ui_LabelInfoTemp, str_lcd);
#endif
	ui_next_state.info_temp = false;
	//-------------------------------
    // 3. Выбор экрана через таблицу (Оптимизация)
    lv_obj_t ** target_scr = &ui_ScreenStart;
    void (*target_init)(void) = ui_ScreenStart_screen_init;

    for (uint8_t i = 0; i < sizeof(scr_map)/sizeof(scr_map[0]); i++) {
        if (scr_map[i].state == current_state) {
            target_scr = (lv_obj_t **)scr_map[i].screen_obj;
            target_init = scr_map[i].init_func;
            break;
        }
    }
    //-------------------------------
    // 4. Логика смены экрана
	if(lv_scr_act() != *target_scr) {
		my_screens = *target_scr;
		_ui_screen_change(target_scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, target_init);
		screen_was_changed = true;
	}


    if(screen_was_changed) {
        lv_timer_handler(); // Даем создать объекты

        if (IS_STATE_SCREEN() == STATE_SCREEN_MEASURE) {
            // Принудительно вызываем, так как экран новый
            LCD_init_DisplayChannelScreenMeasurements(pSnsCfg, lcd_template);
            last_tid = lcd_template;
        }
        else if ((IS_STATE_SCREEN() == STATE_SCREEN_CALIB_ZERO_END_MULTI) ||
        		(IS_STATE_SCREEN() == STATE_SCREEN_CALIB_SPAN_END_MULTI)) {
        	// Принудительно вызываем, так как экран новый
        	if((lcd_template == 0) || (lcd_template == 1)){
        		last_tid = lcd_template + 2;
        	}
			LCD_init_DisplayChannelScreenMeasurements(pSnsCfg, last_tid);
//			last_tid = lcd_template + 2;
        }
        else if(IS_STATE_SCREEN() == STATE_SCREEN_WARM_AUTO_CALIB){
        	// Принудительно вызываем, так как экран новый
        	if((lcd_template == 0) || (lcd_template == 1)){
        		last_tid = lcd_template + 2;
        	}
        	LCD_init_DisplayChannelScreenMeasurements(pSnsCfg, last_tid);
        }
        state_screen_change = 1;
    }
    else if (IS_STATE_SCREEN() == STATE_SCREEN_MEASURE) {
        // Выполняем ТОЛЬКО если изменился номер шаблона
        if (lcd_template != last_tid) {
            LCD_init_DisplayChannelScreenMeasurements(pSnsCfg, lcd_template);
            last_tid = lcd_template;
            lv_refr_now(NULL); // Мгновенно перерисовываем изменения
        }
    }
	return state_screen_change;
}

//===========================================================================================================================
void LCD_Image_SET(uint8_t data)
{
	SET_MIRAX(data);
	SET_LORA(data);
	SET_GPS(data);
	SET_BLE(data);
	SET_MUTE(data);
	SET_EXCHANGE(data;)
	SET_LTE(data);
	SET_LTE_VEC(data);
	SET_FAN(data);
}

//===========================================================================================================================
void ui_reset_visibility_flags(void)
{
    ui_next_state.info_str   	= true;
    ui_next_state.info_temp  	= true;
    ui_next_state.head_enter 	= true;
    ui_next_state.head_out   	= true;
    ui_next_state.head_menu  	= true;
    ui_next_state.head_menu_up 	= true;
    ui_next_state.head_timer 	= true;
}

//===========================================================================================================================
void ui_apply_visibility(void)
{
    // Благодаря нашей "умной" ui_set_obj_hidden,
    // LVGL дернется только если реальное состояние изменилось.
    ui_set_obj_hidden(ui_ImageMirax,     ui_next_state.mirax);
    ui_set_obj_hidden(ui_ImageBattery,   ui_next_state.battery_charge);
    ui_set_obj_hidden(ui_LabelInfoStr,   ui_next_state.info_str);
    ui_set_obj_hidden(ui_LabelInfoTemp,  ui_next_state.info_temp);
    ui_set_obj_hidden(ui_PanelHeadEnter, ui_next_state.head_enter);
    ui_set_obj_hidden(ui_PanelHeadOut,   ui_next_state.head_out);
    ui_set_obj_hidden(ui_PanelHeadMenu,  ui_next_state.head_menu);
    ui_set_obj_hidden(ui_PanelHeadMenuUp,ui_next_state.head_menu_up);
    ui_set_obj_hidden(ui_PanelHeadTimer, ui_next_state.head_timer);

    // Иконки
    ui_set_obj_hidden(ui_ImageLora,      ui_next_state.lora);
//    ui_set_obj_hidden(ui_ImageGPS,       ui_next_state.gps);
    ui_set_obj_hidden(ui_ImageBLE,       ui_next_state.ble);
    ui_set_obj_hidden(ui_ImageMuteOn,    ui_next_state.mute);
    ui_set_obj_hidden(ui_ImageExchange,    ui_next_state.exchange);
    ui_set_obj_hidden(ui_ImageLTE,       ui_next_state.lte);
    ui_set_obj_hidden(ui_ImageLTEvec,    ui_next_state.lte_vec);
    ui_set_obj_hidden(ui_ImageFan,       ui_next_state.fan);
}

//===========================================================================================================================
void LCD_Menu(uint8_t num)
{
    // 1. Формируем список элементов в зависимости от модели
	lv_obj_t * labels[] = {ui_LabelInfo, ui_LabelSetting, ui_LabelServicemenu, ui_LabelBackmenu};
	lv_obj_t * image[] = {ui_ImageInfo, ui_ImageSetting, ui_ImageServicemenu, ui_ImageBackmenu};
	lv_obj_t * panels[] = {ui_PanelInfo, ui_PanelSetting, ui_PanelServicemenu, ui_PanelBackmenu};

    const uint8_t count = 4;//sizeof(labels) / sizeof(labels[0]);

    for (uint8_t i = 0; i < count; i++) {
        bool should_hide = false;

        #if (CONFIG_MODEL_LCD == 0)
            // Модель 1: Все 4 элемента помещаются, ничего не скрываем (всегда false)
            should_hide = false;
        #elif (CONFIG_MODEL_LCD == 1)
            const uint8_t split_idx  = 3;
            // Модель 0: Логика разделения (если выбран последний — скрываем остальные, и наоборот)
            should_hide = (num >= split_idx) ? (i < split_idx) : (i >= split_idx);
        #endif

        ui_set_obj_hidden(panels[i], should_hide);
        ui_menu_item_set_focus(labels[i], panels[i], (num == i), false);
        ui_menu_item_set_recolor(image[i], (num == i));
    }
}

//===========================================================================================================================
// ------------------------------------------------------------------
// Вариант 1: Одна панель (220px), ui_PanelHeadMenuUp скрыта
// ------------------------------------------------------------------
void LCD_SubMenu_Single(uint8_t num, uint16_t roller)
{
    if (roller >= ROLLER_MENU_END) return;

    // 1. Текст на основной панели
    ui_set_label_text(ui_LabelHeadMenu, _(menu_labels[roller])); // или ваш источник текста

    // 2. Скрываем панель "Вперёд"
    ui_next_state.head_menu_up = true;

    // 3. Ширина основной панели на весь экран
#if (CONFIG_MODEL_LCD == 0)
    ui_set_obj_width(ui_PanelHeadMenu, 220);
#elif (CONFIG_MODEL_LCD == 1)
    ui_set_obj_width(ui_PanelHeadMenu, 280);
#endif

    // 4. Фокус только на основном элементе
    // <-- сюда вставите свою логику подсветки в процессе
    ui_menu_item_set_focus(ui_LabelHeadMenu, ui_PanelHeadMenu, (num == 0), false);
    ui_menu_item_set_recolor(ui_ImageHeadMenu, (num == 0));
	//-------------------------------
    ui_next_state.head_menu = false;
	//-------------------------------
}

// ------------------------------------------------------------------
// Вариант 2: Две панели (190px + ui_PanelHeadMenuUp)
// ------------------------------------------------------------------
void LCD_SubMenu_Double(uint8_t num, uint16_t roller)
{
    if (roller >= ROLLER_MENU_END) return;

    // 1. Текст на основной панели
    ui_set_label_text(ui_LabelHeadMenu, _(menu_labels[roller]));

    // 2. Показываем панель "Вперёд" (картинка уже есть внутри)
    ui_next_state.head_menu_up = false;

    // 3. Сужаем основную панель под вторую
#if (CONFIG_MODEL_LCD == 0)
    ui_set_obj_width(ui_PanelHeadMenu, 190);
#elif (CONFIG_MODEL_LCD == 1)
    ui_set_obj_width(ui_PanelHeadMenu, 250);
#endif
    // 4. Фокус на основном элементе
    ui_menu_item_set_focus(ui_LabelHeadMenu, ui_PanelHeadMenu, (num == 0), false);
    ui_menu_item_set_recolor(ui_ImageHeadMenu, (num == 0));

    // 5. Фокус на панели "Вперёд" (текста нет, передаём NULL)
    // <-- сюда вставите свою логику подсветки в процессе
    ui_menu_item_set_focus(ui_PanelHeadMenuUp, ui_PanelHeadMenuUp, (num == 1), false);
    ui_menu_item_set_recolor(ui_ImageHeadMenuUp, (num == 1));
	//-------------------------------
    ui_next_state.head_menu = false;
	//-------------------------------
}

//===========================================================================================================================
void LCD_SubMenuInfo(SNS_CFG *pSnsCfg, uint8_t num)
{
    uint8_t current_page = num;
    LCD_SubMenu_Single(0, ROLLER_MENU_INFO);

    struct tm *tm_ptr = gmtime(&tmp_current);

    // Буферы для значений
    static char str_date[12], str_time[12];
    static char str_sn_dev[24];
    static char str_hw[12], str_fw[12];
    static char str_k_sn[COUNT_CHAN][15];
    static char str_k_val[COUNT_CHAN][12];
    static char str_limits_val[3][12];
    static char str_err_buf[COUNT_CHAN][64];
    static char str_sys_err_buf[64];
    static char env_temp_str[15];
    static char env_press_str[15];
    static char env_hum_str[15];

    // Массивы указателей
    const char * labels[60];
    const char * values[60];
    uint8_t total_count = 0;

    // --- 1. Дата и Время (всегда отображаются) ---
    sprintf(str_date, "%02d.%02d.%04d", tm_ptr->tm_mday, tm_ptr->tm_mon + 1, tm_ptr->tm_year + 1900);
    labels[total_count] = _("Дата"); values[total_count++] = str_date;

    sprintf(str_time, "%02d:%02d", tm_ptr->tm_hour, tm_ptr->tm_min);
    labels[total_count] = _("Время"); values[total_count++] = str_time;

    // --- 2. Заводской номер прибора (всегда отображается) ---
    // Инициализация флага формата на этапе компиляции
    uint8_t is_7_digits = 0; // 0: %08d, 1: %07d
#if (MIRAX_BACS_BUILD == MIRAX)
    #if ((DEVICE_NUMBER == Device2_Pro) || (DEVICE_NUMBER == Device2_Pro_1) || (DEVICE_NUMBER == Device3_Pro))
        is_7_digits = 1;
    #endif
#endif
    uint32_t serial = ((uint32_t)pSnsCfg->Config_common.SerialHi << 16) | pSnsCfg->Config_common.SerialLo;
    char sn_buffer[24];
    strcpy(sn_buffer, (char*)SN_Label_Prefix[MIRAX_BACS_BUILD]);

    if (is_7_digits) {
        sprintf(sn_buffer + strlen(sn_buffer), "%07d", serial);
    } else {
        sprintf(sn_buffer + strlen(sn_buffer), "%08d", serial);
    }

    sprintf(str_sn_dev, "%s", sn_buffer);
    labels[total_count] = _("Зав.номер"); values[total_count++] = str_sn_dev;

    // --- 3. Версии HW/FW (всегда отображаются) ---
    uint16_t hw_ver = pSnsCfg->Config_common.HardwareVersion;
    sprintf(str_hw, "%d.%d", hw_ver / 10, hw_ver % 10);
    labels[total_count] = _("Версия HW"); values[total_count++] = str_hw;

    // Device_About определен глобально в этом файле в зависимости от DEVICE_NUMBER
    labels[total_count] = _("Имя FW");
    values[total_count++] = (char*)Device_Label[MIRAX_BACS_BUILD];

    #ifdef FIRMWARE_VERSION
        uint16_t fw_ver = FIRMWARE_VERSION;
        sprintf(str_fw, "%d.%02d", fw_ver / 100, fw_ver % 100);
    #else
        sprintf(str_fw, "0.00");
    #endif
    labels[total_count] = _("Версия FW"); values[total_count++] = str_fw;

    // --- 4. Цикл по каналам ---
    for (uint8_t ch = 0; ch < COUNT_CHAN; ch++) {
        if (TESTBIT(pSnsCfg->Sensor[ch].State, ST_BIT_CHANNEL_TURN_OFF)) continue;

        // 4.1 Серийный номер сенсора
        // ПРОВЕРКА: Не отображать, если пустой или все нули
        if (Check_SerialNumberChannel(ch)) {

            // Формируем строку с префиксом "EC"
            snprintf(str_k_sn[ch], sizeof(str_k_sn[ch]), "EC%s", pSnsCfg->Sensor[ch].SerialNum);

            static char name_buf_sn[COUNT_CHAN][20];
            sprintf(name_buf_sn[ch], "%s%d %s", num_channel[APPLANG], ch + 1, _("Сер.номер"));

            labels[total_count] = name_buf_sn[ch];
            values[total_count++] = str_k_sn[ch];
        }

        // 4.2 Диапазон (ДИ)
        float conversion = Get_Conversion_factor_rf(ch);
        float range_val = pSnsCfg->Sensor[ch].Limit[3]*conversion;
        // ПРОВЕРКА: Не отображать, если значение <= 0
        if (range_val > 0.0f) {
            uint8_t disk = (pSnsCfg->Sensor[ch].DeviceSetting & MEASURE_DISK) >> OFFSET_DISK;
            sprintf(str_k_val[ch], "%.*f", disk, range_val);

            uint8_t unit_code = pSnsCfg->Sensor[ch].DeviceSetting & MEASURE_MASK;
            if(unit_code >= LCD_STR_UNIT_END) unit_code = 0;

            static char name_buf_di[COUNT_CHAN][20];
            sprintf(name_buf_di[ch], "%s%d %s, %s", num_channel[APPLANG], ch + 1, _("ДИ"), Menu_unit[APPLANG][unit_code]);

            labels[total_count] = name_buf_di[ch];
            values[total_count++] = str_k_val[ch];
        }

        // 4.3 Лимиты (STEL, TWA, High)
        // ПРОВЕРКА: Не отображать, если значение <= 0

        // STEL
        if (pSnsCfg->Sensor[ch].Limit_Stel > 0.0f) {
            static char name_stel[COUNT_CHAN][20];
            static char val_stel[COUNT_CHAN][12];
            sprintf(name_stel[ch], "%s%d STEL", num_channel[APPLANG], ch + 1);
            sprintf(val_stel[ch], "%.*f", 0, pSnsCfg->Sensor[ch].Limit_Stel*conversion);
            labels[total_count] = name_stel[ch];
            values[total_count++] = val_stel[ch];
        }

        // TWA
        if (pSnsCfg->Sensor[ch].Limit_Twa > 0.0f) {
            static char name_twa[COUNT_CHAN][20];
            static char val_twa[COUNT_CHAN][12];
            sprintf(name_twa[ch], "%s%d TWA", num_channel[APPLANG], ch + 1);
            sprintf(val_twa[ch], "%.*f", 0, pSnsCfg->Sensor[ch].Limit_Twa*conversion);
            labels[total_count] = name_twa[ch];
            values[total_count++] = val_twa[ch];
        }

        // High
        if (pSnsCfg->Sensor[ch].Limit[4] > 0.0f) {
            static char name_high[COUNT_CHAN][20];
            static char val_high[COUNT_CHAN][12];
            sprintf(name_high[ch], "%s%d HIGH", num_channel[APPLANG], ch + 1);
            sprintf(val_high[ch], "%.*f", 0, pSnsCfg->Sensor[ch].Limit[4]*conversion);
            labels[total_count] = name_high[ch];
            values[total_count++] = val_high[ch];
        }

        // 4.4 Ошибки (Отображаются всегда, даже если "-" )
        str_err_buf[ch][0] = '\0';
        bool has_err = false;
        uint32_t state_err_chan = pSnsCfg->Sensor[ch].StateErr;

        if (state_err_chan & ERROR_MASK_ST_1) {
            for (int i = 0; i < ERROR_COUNT_ST_1; i++) {
                if (TESTBIT(state_err_chan, i)) {
                    if (has_err) strcat(str_err_buf[ch], ", ");
                    uint8_t err_code = (uint8_t)(i + (ch << 4));
                    sprintf(str_err_buf[ch] + strlen(str_err_buf[ch]), "%02X", err_code);
                    has_err = true;
                }
            }
        }
        if (TESTBIT(state_err_chan, ST_BIT_ERR_TEMP_SHOCK)) {
            if (has_err) strcat(str_err_buf[ch], ", ");
            sprintf(str_err_buf[ch] + strlen(str_err_buf[ch]), "TS");
            has_err = true;
        }

        if(has_err) {
            static char name_err[COUNT_CHAN][20];
            sprintf(name_err[ch], "%s%d %s", num_channel[APPLANG], ch + 1, _("Ошибки"));

            labels[total_count] = name_err[ch];
            values[total_count++] = str_err_buf[ch];
        } else {
//            values[total_count++] = "-";
        }
    }

    // --- 5. Системные ошибки ---
    if (pSnsCfg->Config_common.StateErr & ERROR_MASK_ST_2) {
        str_sys_err_buf[0] = '\0';
        bool has_sys_err = false;
        uint32_t state_err_sys = pSnsCfg->Config_common.StateErr;

        for (int i = 0; i < ERROR_COUNT_ST_2; i++) {
            if (TESTBIT(state_err_sys, i)) {
                if (has_sys_err) strcat(str_sys_err_buf, ", ");
                sprintf(str_sys_err_buf + strlen(str_sys_err_buf), "F%X", i);
                has_sys_err = true;
            }
        }
        if (has_sys_err) {
            labels[total_count] = _("Систем. ош.");
            values[total_count++] = str_sys_err_buf;
        }
    }

    // --- 5.1. Температура, давление и влажность ---
    snprintf(env_temp_str, sizeof(env_temp_str), "%d °С",
             (int)pSnsCfg->Config_common.Temperature);
    labels[total_count] = env_labels[APPLANG][0]; values[total_count++] = env_temp_str;


    snprintf(env_press_str, sizeof(env_press_str), "%d mmHg",
             (int)pSnsCfg->Config_common.Pressure);
    labels[total_count] = env_labels[APPLANG][1]; values[total_count++] = env_press_str;


    snprintf(env_hum_str, sizeof(env_hum_str), "%d %%",
             (int)pSnsCfg->Config_common.Humidity);
    labels[total_count] = env_labels[APPLANG][2]; values[total_count++] = env_hum_str;

    // --- 6. Расчет страниц ---
#if (CONFIG_MODEL_LCD == 0)
    const uint8_t LINES_PER_PAGE = 7;
#elif (CONFIG_MODEL_LCD == 1)
    const uint8_t LINES_PER_PAGE = 5;
#endif

    if (total_count == 0) {
        info_total_pages = 1;
    } else {
        info_total_pages = (total_count + LINES_PER_PAGE - 1) / LINES_PER_PAGE;
    }

    if (current_page >= info_total_pages) {
        current_page = 0;
    }

    uint8_t start_idx = current_page * LINES_PER_PAGE;
    uint8_t count_on_page = LINES_PER_PAGE;

    if ((start_idx + count_on_page) > total_count) {
        count_on_page = total_count - start_idx;
    }

    // --- 7. Отрисовка ---
    LCD_Fill_Info_Page(ui_LabelInfoLeft, ui_LabelInfoRight, labels, values, start_idx, count_on_page);
}

//===========================================================================================================================
void LCD_SubMenuSetting(SNS_CFG *pSnsCfg, uint8_t num)
{
	//-------------------------------
	LCD_SubMenu_Single(num, ROLLER_MENU_SETTING);
	//-------------------------------
#if (CONFIG_MODEL_LCD == 0)
	uint8_t count_panels = 5;
    lv_obj_t * labels[] = { ui_LabelSetting1, ui_LabelSetting2, ui_LabelSetting3,
    		ui_LabelSetting4, ui_LabelSetting5 };
    lv_obj_t * image[] = { ui_ImageSetting1, ui_ImageSetting2, ui_ImageSetting3,
    		ui_ImageSetting4, ui_ImageSetting5};
    lv_obj_t * panels[] = { ui_PanelSetting1, ui_PanelSetting2, ui_PanelSetting3,
    		ui_PanelSetting4, ui_PanelSetting5 };

    // Логика Насоса (Pump)
    bool pump_on = TESTBIT(pSnsCfg->Config_common.State, ST_COMMON_BIT_TURN_ON_PUMP);
    ui_set_label_text(ui_LabelSetting5, _(pump_options[pump_on]));
    if(pump_on){
    	ui_set_img_src(ui_ImageSetting5, &ui_img_imagemenu_checked_png);
    }
    else{
    	ui_set_img_src(ui_ImageSetting5, &ui_img_imagemenu_unchecked_png);
    }
#elif (CONFIG_MODEL_LCD == 1)
    uint8_t count_panels = 5;
    lv_obj_t * labels[] = { ui_LabelSetting1, ui_LabelSetting2, ui_LabelSetting3,
    		ui_LabelSetting4, ui_LabelSetting5};
    lv_obj_t * image[] = { ui_ImageSetting1, ui_ImageSetting2, ui_ImageSetting3,
    		ui_ImageSetting4, ui_ImageSetting5};
    lv_obj_t * panels[] = { ui_PanelSetting1, ui_PanelSetting2, ui_PanelSetting3,
    		ui_PanelSetting4, ui_PanelSetting5};
//    for (int i = 0; i < count_panels; i++) {
//        // Если i == 4 (пятый элемент) и num == 5, то скрываем (true), иначе логика из вашего примера
//        bool should_hide = (num == 5) ? (i != 4) : (i == 4);
//        ui_set_obj_hidden(panels[i], should_hide);
//    }
#endif

    for(int i = 0; i < count_panels; i++) {
    	// Если i == 5 (ui_PanelSetting6), включаем прозрачность для невыбранного состояния
    	bool transparent = (i == 4);
    	ui_menu_item_set_focus(labels[i], panels[i], (num == (i + 1)), transparent);
    	ui_menu_item_set_recolor(image[i], (num == (i + 1)));
    }
}

void LCD_SubMenuSettingChan(SNS_CFG *pSnsCfg, uint8_t num, uint16_t enter, CALIB_CFG *calib)
{
	uint16_t count_chan_turn = 0;
	uint16_t offset_num_label = 1;
	//-------------------------------
	if(calib != NULL){
		LCD_SubMenu_Double(num, enter);
		offset_num_label = 2;
	}
	else{
		LCD_SubMenu_Single(num, enter);
		offset_num_label = 1;
	}
	//-------------------------------
#if (CONFIG_MODEL_LCD == 0)
	lv_obj_t * all_labels[] = { ui_LabelChannel0, ui_LabelChannel1, ui_LabelChannel2, ui_LabelChannel3,
				ui_LabelChannel4, ui_LabelChannel5, ui_LabelChannel6, ui_LabelChannel7 };
	lv_obj_t * all_image[] = { ui_ImageChannel0, ui_ImageChannel1, ui_ImageChannel2, ui_ImageChannel3,
				ui_ImageChannel4, ui_ImageChannel6, ui_ImageChannel6, ui_ImageChannel7};
#elif (CONFIG_MODEL_LCD == 1)
	lv_obj_t * all_labels[] = { ui_LabelChannel0, ui_LabelChannel1, ui_LabelChannel2, ui_LabelChannel3};
	lv_obj_t * all_image[] = { ui_ImageChannel0, ui_ImageChannel1, ui_ImageChannel2, ui_ImageChannel3};
#endif
	lv_obj_t * active_labels[COUNT_CHAN];

    for(uint8_t chan = CHAN0; chan < COUNT_CHAN; chan++){
		if(!TESTBIT(pSnsCfg->Sensor[chan].State, ST_BIT_CHANNEL_TURN_OFF)){
			if(calib != NULL){
				ui_set_obj_hidden(all_image[chan], false);
				if(TESTBIT(calib->calib_flag_disp, chan)){
					ui_set_img_src(all_image[chan], &ui_img_imagemenu_checked_png);
				}
				else{
					ui_set_img_src(all_image[chan], &ui_img_imagemenu_unchecked_png);
				}
			}
			else{
				ui_set_obj_hidden(all_image[chan], true);
			}
			ui_set_obj_hidden(all_labels[chan], false);
			active_labels[count_chan_turn] = all_labels[chan];
			if(pSnsCfg->Sensor[chan].MolecularFormula[0] == 0){
				sprintf(str_lcd, "-");
			}
			else{
				sprintf(str_lcd, "%s", pSnsCfg->Sensor[chan].MolecularFormula);
			}
			ui_set_label_text(active_labels[count_chan_turn], str_lcd);
			count_chan_turn++;
		}
		else{
			ui_set_obj_hidden(all_labels[chan], true);
		}
	}


    for(int i = 0; i < count_chan_turn; i++) {
        ui_menu_item_set_focus(active_labels[i], active_labels[i], (num == (i + offset_num_label)), false);
    }
}

void LCD_SubMenuSettingSet(SNS_CFG *pSnsCfg, uint8_t config, uint8_t activrank, uint8_t num, uint8_t rank, uint8_t state_flashing, uint8_t chan, float data_f, struct tm*tm_ptr)
{
	float conversion = Get_Conversion_factor_rf(chan);
	uint8_t disk = (pSnsCfg->Sensor[chan].DeviceSetting&MEASURE_DISK)>>OFFSET_DISK;
	float data_scr = 0;
	//-------------------------------
    if(activrank == 1){
    	if(config == 0){
			if(num == 1){
				LCD_Data(tm_ptr, rank, state_flashing);
			}
			else if(num == 2){
				LCD_Time(tm_ptr, rank, state_flashing);
			}
    	}
    	else if(config == 1){
    		uint32_t data_u32 = (uint32_t)data_f;
    		data_f = data_f;
//    		if(num == 2){
//    			LCD_Digit_flashing(&pSnsCfg->Sensor[chan], data_f, rank, state_flashing, ROLLER_ENTER_CONC);
//    		}
    		if(num == 1){
    			LCD_Digit_Measure(data_u32, rank, state_flashing);
    		}
    		else if(num == 2){
    			LCD_Digit_Discretenes(data_u32, rank, state_flashing);
    		}
    		else if(num == 3){
    			LCD_Digit_flashing(&pSnsCfg->Sensor[chan], data_f, rank, state_flashing, ROLLER_ENTER_LIMIT1);
    		}
    		else if(num == 4){
    			LCD_Digit_flashing(&pSnsCfg->Sensor[chan], data_f, rank, state_flashing, ROLLER_ENTER_LIMIT2);
    		}
    		else if(num == 5){
    			LCD_Digit_flashing(&pSnsCfg->Sensor[chan], data_f, rank, state_flashing, ROLLER_ENTER_LIMIT3);
    		}
    	}
    	else if(config == 2){

    	}
    }
    else{
    	//-------------------------------
    	if(config == 0){
    		LCD_SubMenu_Single(num, ROLLER_MENU_SETTING_DATA_TIME);
    	}
    	else if(config == 1){
    		LCD_SubMenu_Single(num, ROLLER_MENU_SETTING_LIMIT);
    	}
    	//-------------------------------
    //	else if(config == 2){
    //		LCD_SubMenu_Single(num, ROLLER_MENU_SETTING_UNIT_MEAS);
    //	}
    	//-------------------------------
    //	lv_obj_t *cont_cfg2[] = { ui_ContainerMeas, ui_ContainerDisk };
    //    lv_obj_t *cont_cfg1[] = { ui_ContainerGas, ui_ContainerMeas, ui_ContainerConc, ui_ContainerLimit1, ui_ContainerLimit2, ui_ContainerLimit3 };
    //    lv_obj_t *cont_cfg0[] = { ui_ContainerData, ui_ContainerTimeSet };

    	lv_obj_t *labels1_cfg0[] = { ui_LabelDataSet1, ui_LabelTimeSet1 };
    	lv_obj_t *labels1_cfg1[] = { ui_LabelMeas1, ui_LabelDisk1, ui_LabelLimit11, ui_LabelLimit21, ui_LabelLimit31 };
        lv_obj_t *labels1_cfg2[] = { ui_LabelMeas1, ui_LabelDisk1 };

        ui_set_obj_hidden(ui_ContainerMeas,     !(config == 1 || config == 2));
        ui_set_obj_hidden(ui_ContainerDisk,     !(config == 1 || config == 2));
        ui_set_obj_hidden(ui_ContainerGas,      !(config == 1 || config == 2));
    //    ui_set_obj_hidden(ui_ContainerConc,     !(config == 1));
        ui_set_obj_hidden(ui_ContainerLimit1,   !(config == 1));
        ui_set_obj_hidden(ui_ContainerLimit2,   !(config == 1));
        ui_set_obj_hidden(ui_ContainerLimit3,   !(config == 1));
        ui_set_obj_hidden(ui_ContainerData,     !(config == 0));
        ui_set_obj_hidden(ui_ContainerTimeSet,  !(config == 0));

        uint8_t count = 0;
        uint8_t offset = (config == 1) ? 1 : 1;
        lv_obj_t **current_labels;
        if (config == 2) {
            current_labels = labels1_cfg2;
            count = 2; // ui_LabelMeas1, ui_LabelDisk1
        } else if (config == 1) {
            current_labels = labels1_cfg1;
            count = 5; // ui_LabelMeas1, ui_LabelConc1, ...
        } else {
            current_labels = labels1_cfg0;
            count = 2; // ui_LabelDataSet1, ui_LabelTimeSet1
        }

    	for(int i = 0; i < count; i++){
			ui_menu_item_set_focus(current_labels[i], current_labels[i], (num == (i + offset)), false);
		}
    	if(config == 1){
    		//-------------------------------
    		sprintf(str_lcd, "%s", pSnsCfg->Sensor[chan].MolecularFormula);
    		ui_set_label_text(ui_LabelGas1, str_lcd);
    		LCD_DisplayUnitMeasure(&pSnsCfg->Sensor[chan], DISP, ui_LabelMeas1);
    		sprintf(str_lcd, "%d", disk);
    		ui_set_label_text(ui_LabelDisk1, str_lcd);
    		//-------------------------------
//    		data_scr = (pSnsCfg->Sensor[chan].Prescaler_ConcCalibrGas*conversion) + 0.4 / pow(10, disk);
//    		sprintf(str_lcd, "%.*f", disk, data_scr);
//    		ui_set_label_text(ui_LabelConc1, str_lcd);
    		//-------------------------------
            for(int i = 0; i < 3; i++) {
                uint32_t current_state = pSnsCfg->Sensor[chan].State;
                char arrow = ' ';
                bool is_greater = false;

                if(i == 0){
                	data_scr = (pSnsCfg->Sensor[chan].Limit[0]*conversion) + 0.4 / pow(10, disk);
                	is_greater = !TESTBIT(current_state, ST_BIT_TYPE_LIMIT_1);
                }
                else if(i == 1){
                	data_scr = (pSnsCfg->Sensor[chan].Limit[1]*conversion) + 0.4 / pow(10, disk);
                	is_greater = !TESTBIT(current_state, ST_BIT_TYPE_LIMIT_2);
                }
                else if(i == 2){
                	data_scr = (pSnsCfg->Sensor[chan].Limit[2]*conversion) + 0.4 / pow(10, disk);
                	is_greater = !TESTBIT(current_state, ST_BIT_TYPE_LIMIT_3);
                }

                arrow = is_greater ? '>' : '<';

                // sprintf выполняется только при реальном изменении!
                sprintf(str_lcd, "%c%.*f", arrow, disk, data_scr);

                if(i == 0) ui_set_label_text(ui_LabelLimit11, str_lcd);
                if(i == 1) ui_set_label_text(ui_LabelLimit21, str_lcd);
                if(i == 2) ui_set_label_text(ui_LabelLimit31, str_lcd);
            }
    		//-------------------------------
    	}
    	else if(config == 0){
    		//-------------------------------
    		sprintf(str_lcd, "%.2d.%.2d.%.4d", tm_ptr->tm_mday, tm_ptr->tm_mon + 1, (tm_ptr->tm_year + 1900));
    		ui_set_label_text(ui_LabelDataSet1, str_lcd);
    		//-------------------------------
    		sprintf(str_lcd, "%.2d:%.2d", tm_ptr->tm_hour, tm_ptr->tm_min);
    		ui_set_label_text(ui_LabelTimeSet1, str_lcd);
    		//-------------------------------
    	}
//    	else if(config == 2){
//    		sprintf(str_lcd, "%s", pSnsCfg->Sensor[chan].MolecularFormula);
//    		ui_set_label_text(ui_LabelGas1, str_lcd);
//    		LCD_DisplayUnitMeasure(&pSnsCfg->Sensor[chan], DISP, ui_LabelMeas1);
//    		sprintf(str_lcd, "%d", disk);
//    		ui_set_label_text(ui_LabelDisk1, str_lcd);
//    	}
    }
}

void LCD_SubMenuSettingSetCalib(SNS_CFG *pSnsCfg, float data_f, uint8_t chan, uint8_t rank, uint8_t state_flashing, uint16_t enter)
{
	//-------------------------------
//	LCD_SubMenu(num, enter);
	//-------------------------------
	LCD_Digit_flashing(&pSnsCfg->Sensor[chan], data_f, rank, state_flashing, ROLLER_ENTER_CONC);
}

void LCD_SubMenuSetting_Panel_Head_Button_1(bool show_roller, int8_t rank, uint16_t enter)
{
	 uint16_t enter_head_out = ROLLER_HEAD_END;
	//-------------------------------
	 LCD_SubMenu_Single(rank, enter);
	if(enter == ROLLER_MENU_SERVICE_CALIB_RANGE){
		enter_head_out = ROLLER_HEAD_COUNT_CALIB;
	}
	else if(enter == ROLLER_MENU_SERVICE_CALIB_ZERO){
		enter_head_out = ROLLER_HEAD_COUNT_CALIB;
	}
	else if(enter == ROLLER_MENU_SETTING_LANG){
		enter_head_out = ROLLER_HEAD_LANGUAGE;
	}
	else if(enter == ROLLER_MENU_SETTING_RESET_STH){
		enter_head_out = ROLLER_HEAD_CONFIRM;
	}
	else if(enter == ROLLER_MENU_SERVICE_RESTART){
		enter_head_out = ROLLER_HEAD_RESTART;
	}
	else if(enter ==  ROLLER_MENU_SETTING_FACTORY){
		enter_head_out = ROLLER_HEAD_CONFIRM;
	}
	//-------------------------------
	ui_update_input_panel_head_out(NULL, 0, show_roller, rank, 1, 2, enter_head_out);
}

void LCD_SubMenuSetting_Panel_Head_Button_2(bool show_roller, int8_t rank, uint16_t enter)
{
	 uint16_t enter_head_out = ROLLER_HEAD_END;
	//-------------------------------
	 LCD_SubMenu_Single(rank, enter);
	if(enter == ROLLER_MENU_SERVICE_CALIB_RANGE){
		enter_head_out = ROLLER_HEAD_RANGE_GAS;
	}
	else if(enter == ROLLER_MENU_SERVICE_CALIB_ZERO){
		enter_head_out = ROLLER_HEAD_ZERO_GAS;
	}
	//-------------------------------
	ui_update_input_panel_head_out(NULL, 0, show_roller, rank, 1, 2, enter_head_out);
}

void LCD_SubMenuSetting_Panel_Head_Button_3(bool show_roller, int8_t rank, uint16_t enter)
{
	 uint16_t enter_head_out = ROLLER_HEAD_END;
	//-------------------------------
	 LCD_SubMenu_Single(rank, enter);
	if(enter == ROLLER_MENU_SERVICE_CALIB_RANGE){
		enter_head_out = ROLLER_HEAD_CALIB_GAS;
	}
	else if(enter == ROLLER_MENU_SERVICE_CALIB_ZERO){
		enter_head_out = ROLLER_HEAD_CALIB_GAS;
	}
	//-------------------------------
	ui_update_input_panel_head_out(NULL, 0, show_roller, rank, 1, 2, enter_head_out);
}

void LCD_SubMenuSetting_Panel_Head_Button_Meas_1(SNS_CFG_Type *pSnsCfg, uint32_t t1, uint8_t state, int8_t rank, uint16_t enter)
{
	 uint16_t enter_head_out = ROLLER_HEAD_END;
	//-------------------------------
	 LCD_SubMenu_Single(rank, enter);
	if(enter == ROLLER_MENU_SERVICE_CALIB_RANGE){
		enter_head_out = ROLLER_HEAD_CALIB_GAS;
	}
	else if(enter == ROLLER_MENU_SERVICE_CALIB_ZERO){
		enter_head_out = ROLLER_HEAD_CALIB_GAS;
	}
	//-------------------------------
	ui_update_input_panel_head_out_calib(pSnsCfg, t1, state, rank, 1, 2, enter_head_out);
}

void LCD_SubMenuSetting_Panel_Head_Button_Meas_2(SNS_CFG_Type *pSnsCfg, uint32_t t1, uint8_t state, int8_t rank, uint16_t enter)
{
	 uint16_t enter_head_out = ROLLER_HEAD_END;
	//-------------------------------
	 LCD_SubMenu_Single(rank, enter);
	if(enter == ROLLER_MENU_SERVICE_CALIB_RANGE){
		enter_head_out = ROLLER_HEAD_CALIB_WAIT;
	}
	else if(enter == ROLLER_MENU_SERVICE_CALIB_ZERO){
		enter_head_out = ROLLER_HEAD_CALIB_WAIT;
	}
	//-------------------------------
	ui_update_input_panel_head_out_calib(pSnsCfg, t1, state, rank, 1, 2, enter_head_out);
}

void LCD_SubMenuSetting_Panel_Head_Text(bool show_roller, int8_t rank, uint16_t enter)
{
	 uint16_t enter_head_out = ROLLER_HEAD_END;
	//-------------------------------
	 LCD_SubMenu_Single(rank, enter);
	if(enter == ROLLER_MENU_SERVICE_CALIB_RANGE){
		enter_head_out = 3;
	}
	else if(enter == ROLLER_MENU_SERVICE_CALIB_ZERO){
		enter_head_out = 3;
	}
	else if(enter == ROLLER_MENU_SERVICE_RESTART){
		enter_head_out = 4;
	}
	//-------------------------------
	ui_update_input_panel_head_out_text(show_roller, enter_head_out);
}

//===========================================================================================================================
void LCD_SubMenuService(SNS_CFG *pSnsCfg, uint8_t num)
{
	//-------------------------------
	LCD_SubMenu_Single(num, ROLLER_MENU_SERVICE);
	//-------------------------------
#if (CONFIG_MODEL_LCD == 0)
	uint8_t count_panels = 6;
#elif (CONFIG_MODEL_LCD == 1)
	uint8_t count_panels = 3;
#endif
    lv_obj_t * labels[] = { ui_LabelServiceMenu1, ui_LabelServiceMenu2, ui_LabelServiceMenu3,
                           ui_LabelServiceMenu4, ui_LabelServiceMenu5, ui_LabelServiceMenu6 };
    lv_obj_t * panels[] = { ui_PanelServiceMenu1, ui_PanelServiceMenu2, ui_PanelServiceMenu3,
                            ui_PanelServiceMenu4, ui_PanelServiceMenu5, ui_PanelServiceMenu6 };

    for(int i = 0; i < count_panels; i++) {
        // Элементы 4, 5 и 6 (индексы 3, 4, 5) помечаем как прозрачные
        bool is_trans = (i >= 3);
        ui_menu_item_set_focus(labels[i], panels[i], (num == (i + 1)), is_trans);
    }

#if (CONFIG_MODEL_LCD == 0)
    if(TESTBIT(pSnsCfg->Config_common.State, ST_COMMON_BIT_TURN_ON_BLE)){
    	ui_set_img_src(ui_ImageServiceMenu4, &ui_img_imagemenu_checked_png);
	}
	else{
		ui_set_img_src(ui_ImageServiceMenu4, &ui_img_imagemenu_unchecked_png);
	}

	if(TESTBIT(pSnsCfg->Config_common.State, ST_COMMON_BIT_TURN_ON_LORA)){
		ui_set_img_src(ui_ImageServiceMenu5, &ui_img_imagemenu_checked_png);
	}
	else{
		ui_set_img_src(ui_ImageServiceMenu5, &ui_img_imagemenu_unchecked_png);
	}

	if(TESTBIT(pSnsCfg->Config_common.State, ST_COMMON_BIT_TURN_ON_GPS)){
		ui_set_img_src(ui_ImageServiceMenu6, &ui_img_imagemenu_checked_png);
	}
	else{
		ui_set_img_src(ui_ImageServiceMenu6, &ui_img_imagemenu_unchecked_png);
	}
	ui_set_obj_hidden(ui_PanelServiceMenu4, false);
	ui_set_obj_hidden(ui_PanelServiceMenu5, false);
	ui_set_obj_hidden(ui_PanelServiceMenu6, false);
#elif (CONFIG_MODEL_LCD == 1)
	ui_set_obj_hidden(ui_PanelServiceMenu4, true);
	ui_set_obj_hidden(ui_PanelServiceMenu5, true);
	ui_set_obj_hidden(ui_PanelServiceMenu6, true);
#endif
}

//===========================================================================================================================
void LCD_Battery(uint16_t precent, uint16_t state_flashing)
{
	uint16_t precent_disp = precent;
    static TickType_t charge_end_ticks = 0;
    uint8_t is_charging_active = 0;
    TickType_t current_ticks = xTaskGetTickCount();

    if (TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BAT_CHARGE)) {
        // Провод подключен: рассчитываем точку окончания (сейчас + задержка в тиках)
        charge_end_ticks = current_ticks + pdMS_TO_TICKS(2500);
        is_charging_active = 1;
    }
    else {
        // Провода нет: проверяем, не ушло ли текущее время за точку окончания.
        // Защита от переполнения счетчика тиков RTOS через явное вычитание знаковых типов:
        if ((BaseType_t)(charge_end_ticks - current_ticks) > 0) {
            is_charging_active = 1; // Время удержания еще НЕ истекло
        }
    }

	if(is_charging_active){
#if (CONFIG_BATTERY == 1)
		precent_disp = 0;
#endif
		if(((seg_battery_charge_flag == 0) && (state_flashing)) ||
				((seg_battery_charge_flag) && (state_flashing == 0))){
			if((precent_disp + seg_battery_charge_count) >= 100){
				seg_battery_charge_count = 0;
			}
			seg_battery_charge_flag = state_flashing;
			seg_battery_charge_count += 10;
			if((precent_disp + seg_battery_charge_count) > 100){
				seg_battery_charge_count = 100;
			}
		}
		precent_disp = precent_disp + seg_battery_charge_count;
	}
	else{
		seg_battery_charge_count = 0;
		if(precent_disp <= BAT_LIM_PERCENT(BAT_LIM_1)){
			precent_disp = 0;
		}
	}

	ui_set_bar_value(ui_BarBattery, precent_disp, LV_ANIM_OFF);
#if (CONFIG_BATTERY == 1)
	if(is_charging_active){
		sprintf(str_lcd, " ", precent);
	}
	else{
		sprintf(str_lcd, "%d%%", precent);
	}
#else
	sprintf(str_lcd, "%d%%", precent);
#endif
	ui_set_label_text(ui_LabelPercent, str_lcd);
}

//===========================================================================================================================
void LCD_DataTime(struct tm*tm_ptr, uint8_t rank, uint8_t state_flashing)
{
	uint8_t rank_f = 0;
	sprintf(str_lcd, "%.2d.%.2d.%.4d %.2d:%.2d",
			tm_ptr->tm_mday,
			tm_ptr->tm_mon + 1,
			(tm_ptr->tm_year + 1900),
			tm_ptr->tm_hour,
			tm_ptr->tm_min);
	//-------------------------------
	int8_t b_idx = -1; // Индекс мигающего символа

	if (state_flashing) {
		switch(rank) {
			// ДЕНЬ (6-7)
			case 7: b_idx = 0; break; // Десятки дня
			case 6: b_idx = 1; break; // Единицы дня
			// МЕСЯЦ (4-5)
			case 5: b_idx = 3; break; // Десятки месяца
			case 4: b_idx = 4; break; // Единицы месяца
			// ГОД (0-3)
			case 3: b_idx = 6; break; // Тысячи года
			case 2: b_idx = 7; break; // Сотни года
			case 1: b_idx = 8; break; // Десятки года
			case 0: b_idx = 9; break; // Единицы года
			// ЧАСЫ (10-11)
			case 11: b_idx = 11; break; // Десятки часов (после пробела на 10-й позиции)
			case 10: b_idx = 12; break; // Единицы часов
			// МИНУТЫ (8-9)
			case 9: b_idx = 14; break; // Десятки минут (после двоеточия)
			case 8: b_idx = 15; break; // Единицы минут

			default: b_idx = -1; break;
		}
	}

	// Вызываем общую функцию (ОК - rank 12, Back - rank 13)
	ui_update_input_panel_head_enter(str_lcd, rank, b_idx, 12, 13, ROLLER_ENTER_DATATIME);
}

//===========================================================================================================================
void LCD_Data(struct tm*tm_ptr, uint8_t rank, uint8_t state_flashing)
{
	uint8_t rank_f = 0;
	sprintf(str_lcd, "%.2d.%.2d.%.4d", tm_ptr->tm_mday, tm_ptr->tm_mon + 1, (tm_ptr->tm_year + 1900));
	//-------------------------------
	int8_t b_idx = -1;
    if (state_flashing) {
        switch(rank) {
            case 7: b_idx = 0; break; // Десятки дня
            case 6: b_idx = 1; break; // Единицы дня
            case 5: b_idx = 3; break; // Десятки месяца
            case 4: b_idx = 4; break; // Единицы месяца
            case 3: b_idx = 6; break; // Тысячи года
            case 2: b_idx = 7; break; // Сотни года
            case 1: b_idx = 8; break; // Десятки года
            case 0: b_idx = 9; break; // Единицы года
            default: b_idx = -1; break;
        }
    }
    ui_update_input_panel_head_enter(str_lcd, rank, b_idx, 9, 8, ROLLER_ENTER_DATA);
}

//===========================================================================================================================
void LCD_Time(struct tm*tm_ptr, uint8_t rank, uint8_t state_flashing)
{
	uint8_t rank_f = 0;
	sprintf(str_lcd, "%.2d:%.2d", tm_ptr->tm_hour, tm_ptr->tm_min);
    //-------------------------------
    int8_t b_idx = -1;
    if (state_flashing) {
        switch(rank) {
            case 11: b_idx = 0; break; // Десятки часов
            case 10: b_idx = 1; break; // Единицы часов
            case 9:  b_idx = 3; break; // Десятки минут
            case 8:  b_idx = 4; break; // Единицы минут
            default: b_idx = -1; break;
        }
    }
    // Кнопки OK/Back для времени (rank 12 и 13 или ваши значения)
    ui_update_input_panel_head_enter(str_lcd, rank, b_idx, 13, 12, ROLLER_ENTER_TIME);
}

//===========================================================================================================================
// Отображение целого числа и мигание разрядом
void LCD_Digit_int_flashing_password(uint32_t data, uint8_t rank, uint8_t state_flashing)
{
	uint8_t rank_f = 0;
    sprintf(str_lcd, "%03d", data);
    //-------------------------------
	int8_t b_idx = (state_flashing && rank >= 2 && rank <= 4) ? (rank - 2) : -1;

	ui_update_input_panel_head_enter(str_lcd, rank, b_idx, 5, 6, ROLLER_ENTER_PASSWORD);
}

//===========================================================================================================================
// Ввод единицы измерения
void LCD_Digit_Measure(uint32_t data, uint8_t rank, uint8_t state_flashing)
{
    //-------------------------------
	ui_update_input_panel_head_enter(Menu_unit[APPLANG][data], rank, -1, 1, 2, ROLLER_ENTER_MEAS);
}

//===========================================================================================================================
// Ввод дискретности
void LCD_Digit_Discretenes(uint32_t data, uint8_t rank, uint8_t state_flashing)
{
    sprintf(str_lcd, "%d", data);
    //-------------------------------
	ui_update_input_panel_head_enter(str_lcd, rank, -1, 1, 2, ROLLER_ENTER_DISK);
}

//===========================================================================================================================
// Отображение целого числа и мигание разрядом
void LCD_Digit_flashing(SNS_CFG_Type *pSnsCfg, float data, uint8_t rank, uint8_t state_flashing, uint16_t enter)
{
	uint8_t rank_f = 0;
	uint8_t disk = (pSnsCfg->DeviceSetting&MEASURE_DISK)>>OFFSET_DISK;

	uint8_t current_int_digits = sprintf(str_lcd, "%.0f", data);
	if (data < 0) current_int_digits--; // не считаем минус, если есть

	int8_t dot_pos_from_left  = DISP_PARAM_INPUT_DEC_MAX - disk;

	int8_t rank_int_needed = dot_pos_from_left - rank;
	if (rank_int_needed < 1) rank_int_needed = 1;

	uint8_t final_int_width = (current_int_digits > rank_int_needed) ? current_int_digits : rank_int_needed;

	uint8_t width = final_int_width + (disk > 0 ? 1 : 0) + disk;
	sprintf(str_lcd, "%0*.*f", width, disk, data);
    //-------------------------------
	int8_t b_idx = -1;
	if (state_flashing && rank < DISP_PARAM_INPUT_DEC_MAX) {
        int8_t pos_in_int = (dot_pos_from_left - rank);

        if (pos_in_int > final_int_width) {
             b_idx = 0; // Защита
        } else {
             b_idx = final_int_width - pos_in_int;
        }

        // Прыжок через точку
        if (disk > 0 && b_idx >= final_int_width) {
            b_idx++;
        }
	}

	ui_update_input_panel_head_enter(str_lcd, rank, b_idx, DISP_PARAM_INPUT_DEC_MAX, DISP_PARAM_INPUT_DEC_MAX + 1, enter);
}

//===========================================================================================================================
void LCD_TimeHead(struct tm*tm_ptr)
{
	len_str = sprintf(str_lcd, "%.2d:%.2d", tm_ptr->tm_hour, tm_ptr->tm_min);
	//-------------------------------
	ui_set_label_text(ui_LabelTime, str_lcd);
	memset(&str_lcd, 0, sizeof(str_lcd));
	//-------------------------------
}
//===========================================================================================================================

//===========================================================================================================================
void LCD_CountdownSetTimer(uint32_t data, uint16_t roller)
{
	len_str = sprintf(str_lcd, "%d", data);
	//-------------------------------
	ui_set_label_text(ui_LabelSetHeadTimer, _(head_time_labels[roller]));
	ui_set_label_text(ui_LabelSetTimer, str_lcd);
	memset(str_lcd, 0, sizeof(str_lcd));
	//-------------------------------
	ui_next_state.head_timer = false;
	//-------------------------------
}

//===========================================================================================================================
void LCD_CountdownToSOS(uint32_t data, uint32_t state)
{
	LCD_CountdownSetTimer(data, ROLLER_TIMER_SOS);
}

//===========================================================================================================================
void LCD_CountdownToShutdown(uint32_t data)
{
	LCD_CountdownSetTimer(data, ROLLER_TIMER_TURN_OFF);
}

//===========================================================================================================================
void LCD_CountdownToWarm(uint32_t data)
{
	LCD_CountdownSetTimer(data, ROLLER_TIMER_WARM);
}

//===========================================================================================================================
void LCD_ToWarm(SNS_CFG *pSnsCfg, uint16_t num, uint8_t chan)
{
	switch(num)
	{
	case 0:
		LCD_Image_SET(1);
		break;
	case 1:
		LCD_SubMenuInfo(pSnsCfg, chan);
		ui_next_state.head_menu = true;
		sprintf(str_lcd, _("Информация"));
		ui_set_label_text(ui_LabelInfoStr, str_lcd);
		ui_next_state.info_str = false;
//		LCD_GeneralParameters(pSnsCfg);
		break;
	case 2:
//		if(chan < COUNT_CHAN){
//			LCD_SensorParameters(&pSnsCfg->Sensor[chan], 1);
//		}
		break;
	case 3:
		LCD_Image_SET(0);
		LCD_CountdownToWarm((uint32_t)(TEST_TIMER_RUN_RTC(TIMER_RTC_WARM)));
		break;
	case 4:
		LCD_Image_SET(0);
		sprintf(str_lcd, _("Автокалибровка"));
		ui_set_label_text(ui_LabelInfoStr, str_lcd);
		ui_next_state.info_str = false;
	    lv_obj_t * target_scr = ui_ScreenMeasurements;
		if(lv_scr_act() == target_scr) {
			for(uint8_t chan_r = CHAN0; chan_r < COUNT_CHAN; chan_r++){
				if(TEST_STATUS_BIT(ST_BIT_AUTO_ZERO_TURN_ON, chan_r)){
					LCD_float_measure(&pSnsCfg->Sensor[chan_r], GetDisplayIndicationConc(chan_r), 1, NOCHAN);
				}
			}
		}
		break;
	case 5:
		SET_MIRAX(1);
		sprintf(str_lcd, _("Тест"));
		ui_set_label_text(ui_LabelInfoStr, str_lcd);
		ui_next_state.info_str = false;
		break;
	case 6:
		SET_MIRAX(1);
		if(chan == 0){
			sprintf(str_lcd, _("Тест не пройден"));
		}
		else{
			sprintf(str_lcd, _("Тест пройден"));
		}
		ui_set_label_text(ui_LabelInfoStr, str_lcd);
		ui_next_state.info_str = false;
		break;
	case 7:
		SET_MIRAX(1);
		break;
	default:
	break;
	}
}

//===========================================================================================================================
void LCD_Firmware(void)
{
//	SET_MIRAX(1);
	sprintf(str_lcd, _("Прошивка"));
	ui_set_label_text(ui_LabelInfoStr, str_lcd);
	ui_next_state.info_str = false;
}

//===========================================================================================================================
//
void LCD_CalibrationZeroOrSpanMulti(SNS_CFG *pSnsCfg, CALIB_CFG *calib, uint32_t t1, uint16_t enter)
{
	uint8_t state_all = 0;
	uint8_t state = 0;
	float data = 0.0;

	for(uint8_t chan = CHAN0; chan < COUNT_CHAN; chan++){
		state = (calib->calib_flag || calib->calib_flag_state)? LCD_EndCalibrationZeroOrSpan(&pSnsCfg->Sensor[chan], calib):3;
		SETBIT(state_all, state);
		data = GetDisplayIndicationConc(chan);
		LCD_float_chan_calib(&pSnsCfg->Sensor[chan], data, calib, state);
	}
    if(t1){
    	if(TESTBIT(state_all, 1)){
    		sprintf(str_lcd, _("Ошибка"));
    	}
    	else if(TESTBIT(state_all, 2)){
    		sprintf(str_lcd, _("Ок"));
    	}
    	else if(TESTBIT(state_all, 0) || TESTBIT(state_all, 3)){
    		sprintf(str_lcd, time_calib_multi[APPLANG], t1/1000);
    	}

    	ui_set_label_text(ui_LabelInfoStr, str_lcd);
    }

    ui_next_state.info_str = (t1 == 0);
}

//===========================================================================================================================
//const char * t1, bool show_roller, int8_t rank, uint16_t enter
void LCD_CalibrationZeroOrSpan(SNS_CFG_Type *pSnsCfg, CALIB_CFG *calib, uint32_t t1, int8_t rank, uint16_t enter)
{
	uint8_t state = (calib->calib_flag || calib->calib_flag_state)? LCD_EndCalibrationZeroOrSpan(pSnsCfg, calib):3;
	if(calib->calib_flag_start == 1){
		// Ждите
//		state = LCD_EndCalibrationZeroOrSpan(pSnsCfg, calib);
		LCD_SubMenuSetting_Panel_Head_Button_Meas_2(pSnsCfg, t1, state, rank, enter);
	}
	else{
		// Измерение доступ к кнопке сохранить
//		state = calib->calib_flag_disp? LCD_EndCalibrationZeroOrSpan(pSnsCfg, calib):3;
		LCD_SubMenuSetting_Panel_Head_Button_Meas_1(pSnsCfg, t1, state, rank, enter);
	}
}

//===========================================================================================================================
//
uint8_t LCD_EndCalibrationZeroOrSpan(SNS_CFG_Type *pSnsCfg, CALIB_CFG *calib)
{
	uint16_t state = 0;
	uint8_t chan = pSnsCfg->channel;
	bool should_be_visible = (!TESTBIT(pSnsCfg->State, ST_BIT_CHANNEL_TURN_OFF));
	if(!should_be_visible){
		CLRBIT(calib->calib_flag, chan);
		SETBIT(calib->calib_flag_state, chan);
		return 0;
	}
	if(!TESTBIT(calib->calib_flag_disp, chan)){
		return 0;
	}

	if(!TEST_STATUS_BIT(ST_BIT_CHANNEL_TURN_OFF, chan)){
		if(calib->type_calib == ST_BIT_AUTO_ZERO){
			if(TEST_STATUS_BIT(ST_BIT_AUTO_ZERO, chan)){
				if(!TEST_STATUS_ERR_BIT(ST_BIT_SENSOR_FAILED, chan)){
					if(ErrorCalibZeroCount[chan]){
						state = 1;
					}
					else{
						state = 2;
					}
				}
				else if(TEST_STATUS_ERR_BIT(ST_BIT_SENSOR_FAILED, chan)){
					state = 1;
				}
				if(calib->calib_flag){
					state = 3;
				}
			}
		}
		else if(calib->type_calib == ST_BIT_AUTO_SPAN){
			if(TEST_STATUS_BIT(ST_BIT_AUTO_SPAN, chan)){
				if((!TEST_STATUS_ERR_BIT(ST_BIT_SENSOR_FAILED, chan))){
					if(ErrorCalibSpanCount[chan]){
						state = 1;
					}
					else{
						state = 2;
					}
				}
				else if(TEST_STATUS_ERR_BIT(ST_BIT_SENSOR_FAILED, chan)){
					state = 1;
				}
				if(calib->calib_flag){
					state = 3;
				}
			}
		}
	}

	if(calib->calib_flag == 0){
		calib->calib_flag_start = 0;
	}
	else{
		if(state == 3){
			CLRBIT(calib->calib_flag, chan);
			SETBIT(calib->calib_flag_state, chan);
		}
	}
	return state;
}
//===========================================================================================================================
void LCD_BumpTest(SNS_CFG *pSnsCfg, uint8_t num, uint8_t state_flashing)
{

}

//===========================================================================================================================
//
void LCD_BumpTestStatus(SNS_CFG *pSnsCfg, uint8_t num, uint8_t state_flashing)
{

}

#endif
