/**
  ******************************************************************************
  * @file    TFT_indicator.c
  * @brief   Слой индикации прибора из Avis - ВЫКЛЮЧЕН (CONFIG_TYPE_LCD_TFT == 2)
  *
  *          ~2800 строк: DisplayIndication, Button_Run, меню/экраны другого
  *          прибора. Без переноса не соберётся: нужны SNS_CFG_Type, CALIB_CFG,
  *          COUNT_CHAN, значения DEVICE_NUMBER (Device2/Device3_Pro) и функции
  *          Avis (Avis_Run_Temp, DisplayIndication, Button_Run) - PLAN.md
  *          п.29, шаблон в docs/LVGL.md. Включать после переноса измерительных
  *          каналов (этап 3 плана); драйвер панели (TFT.c) от этого слоя не
  *          зависит.
  ******************************************************************************
  */
#include "Vector_main.h"
#if (CONFIG_TYPE_LCD_TFT == 1)   /* слой индикации Avis: ждёт переноса (SNS_CFG_Type,
                                    CALIB_CFG, COUNT_CHAN, DEVICE_NUMBER Device2/3_Pro).
                                    макрос объединён автором - прежний guard
                                    CONFIG_TYPE_LCD в config_device.h не определён;
                                    значения CONFIG_TYPE_LCD_TFT: 0 = экран выключен,
                                    1 = драйвер панели (TFT.c/LCD_platform.c),
                                    2 = слой индикации Avis (этот файл) ВМЕСТО
                                    драйвера - значения взаимоисключающи.
                                    ВНИМАНИЕ: TFT.c/LCD_platform.c закрыты
                                    #if (CONFIG_TYPE_LCD_TFT), т.е. при 2 они
                                    тоже собираются - для чистой сборки режима 2
                                    их guard'ы надо сузить до == 1 */
/* Инклуды проекта - В ОДНОМ МЕСТЕ (агрегатор Vector_main.h): TFT.h (TFT_*,
   LCD_Init + ui.h LVGL/SquareLine), LCD_platform.h (backlight_set),
   TFT_indicator.h (API этого слоя), shared_types/shared_macros, vector_tick
   (VTICK_MS), sensors_status, Sns_Cfg_struct.                              */
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
#define INFO_LINE_HEIGHT            (25)    // Страница «Информация»: высота строки, px (TODO автора "Panel1 height y = 25")
#define LCD_BATTERY_CHARGE_HOLD_MS  (2500u) // Держать анимацию «зарядка» после выдергивания провода, мс (Avis: pdMS_TO_TICKS(2500))
//------------------------------------------------------------------------------
#if ((DEVICE_NUMBER == Dev1))
    const char Device_Label[2][15] = {
        [MIRAX] = "Vector",
        [BACS]  = "VecBACS"
    };
    const char SN_Label_Prefix[2][10] = {
        [MIRAX] = "VEC",
        [BACS]  = "VEC"
    };
#endif
//------------------------------------------------------------------------------
typedef struct {
	uint8_t state;           // Значение из IS_STATE_SCREEN()
	lv_obj_t ** screen_obj;  // Указатель на адрес экрана (ui_Screen...)
	void (*init_func)(void); // Указатель на функцию инициализации
} screen_map_t;

static const screen_map_t scr_map[] = {
	{SCREEN_START,  		&ui_ScreenStart,	ui_ScreenStart_screen_init},

	{SCREEN_MEASURE,		&ui_ScreenMeasurements,      ui_ScreenMeasurements_screen_init},
	// Menu
	{SCREEN_MENU,			&ui_ScreenMenu,		ui_ScreenMenu_screen_init},
	// Info
	{SCREEN_INFO_DEVICE,	&ui_ScreenMenu,		ui_ScreenMenu_screen_init},
	// Setting
	{SCREEN_SETTING,    	&ui_ScreenMenu,		ui_ScreenMenu_screen_init},
	{SCREEN_CONNECT_DEVICE, &ui_ScreenMenu,		ui_ScreenMenu_screen_init},
	// Message
	{SCREEN_VOICE_MESSAGE,  &ui_ScreenMenu,     ui_ScreenMenu_screen_init},
	{SCREEN_TEXT_MESSAGE,   &ui_ScreenMenu,     ui_ScreenMenu_screen_init},
};
//------------------------------------------------------------------------------

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
    [ROLLER_HEAD_NEXT] 		= {"Далее", "Назад", 100, 8, -8, 0, 0, LV_ALIGN_BOTTOM_RIGHT, LV_ALIGN_BOTTOM_LEFT},
    [ROLLER_HEAD_SAVE] 		= {"Сохранить", "Отмена", 100, 8, -8, 0, 0, LV_ALIGN_BOTTOM_RIGHT, LV_ALIGN_BOTTOM_LEFT},
	[ROLLER_HEAD_WAIT] 		= {"Ждите", "Ждите", 100, 8, -8, 0, 0, LV_ALIGN_BOTTOM_RIGHT, LV_ALIGN_BOTTOM_LEFT},
    [ROLLER_HEAD_LANGUAGE]  = {"Русский", "Английский", 175, 0, 0, -30, 30, LV_ALIGN_CENTER, LV_ALIGN_CENTER},
};

//------------------------------------------------------------------------------
// Текст для ui_LabelHeadMenu (строго по порядку enum)
static const char* const text_labels[] = {
    [ROLLER_INFO]               = "Информация",
    [ROLLER_SETTING]            = "Настройка",
    [ROLLER_CONNECT_DEVICE]     = "Контроль подключенного оборудования",
    [ROLLER_PASSWORD]     		= "Введите пароль",
    [ROLLER_TIME]         		= "Время",
    [ROLLER_DATA]      			= "Дата",
    [ROLLER_DATATIME]  			= "Дата и время",
	[ROLLER_TURN_OFF]     		= "Выключение",
	[ROLLER_SOS]      			= "SOS",
};

//------------------------------------------------------------------------------
const char * env_labels[2][3] = {
    { "Температура",  "Давление", "Влажность" },   // RU
    { "Temperature",  "Pressure", "Humidity"  }    // EN
};

const char * time_calib_multi[2] = {
	"%dс.", // RU
	"%ds."  // EN
};

const char * num_channel[2] = {
	"K", // RU
	"C"  // EN
};

//===========================================================================================================================
// Массивы ключей переводов для ошибок (в Flash-памяти)
//const char* const error_keys_chan[10] = {
//    "Порог 1",            // 0 - ST_BIT_LIMIT1
//    "Порог 2",            // 1 - ST_BIT_LIMIT2
//    "Порог 3",            // 2 - ST_BIT_LIMIT3
//    "Прев. STEL",         // 3 - ST_BIT_LIMIT_STEL
//    "Прев. TWA",          // 4 - ST_BIT_LIMIT_TWA
//    "Прев. диапазона",    // 5 - ST_BIT_EXCEEDED_THE_RANGE
//    "Ошибка сенсора",     // 6 - ST_BIT_SENSOR_FAILED
//};

const char* const error_keys_sys[12] = {
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
static lv_obj_t *my_screens = NULL;

uint8_t info_total_pages = 1;       	// Всего страниц (сделано extern в .h для доступа из меню)
ui_visibility_state_t ui_next_state;	// Глобальная или статическая переменная для хранения текущего заказа на отрисовку
//------------------------------------------------------------------------------
// Отображение батареи
static uint16_t seg_battery_charge_count = 0;
static uint16_t seg_battery_charge_flag = 0;
//------------------------------------------------------------------------------
/* Прототипы внутренних помощников слоя индикации.
   ЧАСТЬ ИЗ НИХ ПОКА НЕ ВЫЗЫВАЕТСЯ - заготовка под экраны, которых ещё нет
   (ширина метки, перекрас картинок меню, кнопки панели ввода):
   ui_set_obj_width, ui_menu_item_set_recolor,
   ui_update_input_panel_head_button. Их определения помечены
   __attribute__((unused)) - иначе -Wall даёт -Wunused-function. Из сборки они
   НЕ вырезаются (атрибут только глушит предупреждение): первый же вызов
   включает помощник в работу, снимать атрибут для этого не нужно.
   ui_set_img_src / ui_set_obj_font / ui_set_label_long_mode из этого списка
   убраны: их зовёт экран измерений (LCD_Measure), атрибут unused снят.    */
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
static void ui_menu_item_set_focus(lv_obj_t * obj_text, lv_obj_t * obj_panel, bool is_selected, bool is_transparent);
static void ui_menu_item_set_recolor(lv_obj_t * obj_img, bool is_recolor_on);
static void ui_update_input_panel_head_enter(const char * text, int8_t rank, int8_t blink_idx, uint8_t ok_idx, uint8_t back_idx, uint16_t enter);

static void LCD_Fill_Info_Page(lv_obj_t *left_obj, lv_obj_t *right_obj, const char **labels, const char **values, uint8_t start_idx, uint8_t count_on_page);
//===========================================================================================================================
// Статические вспомогательные функции (не видны извне)
static void LCD_ClearErrorQueue(void);
static void LCD_FormatErrorText(char *out_buf, uint8_t buf_size, const error_queue_item_t *item, SNS_CFG *pSnsCfg);
static uint32_t LCD_CalculateErrorHash(SNS_CFG *pSnsCfg);
static void meas_reset_cache(void);   /* экран измерений пересоздан - геометрию выставить заново */
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

static __attribute__((unused)) void ui_menu_item_set_recolor(lv_obj_t * obj_img, bool is_recolor_on)
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
		// Выключаем выделение, если индекс отрицательный
		ui_set_label_selection(ui_LabelSetEnter, LV_LABEL_TEXT_SELECTION_OFF, LV_LABEL_TEXT_SELECTION_OFF);

    }

    // 3. Подсветка кнопок управления
    // Состояние 0 - активно (инверсия), 1 - обычно
    uint8_t ok_st   = (rank == ok_idx)   ? 1 : 0;
    uint8_t back_st = (rank == back_idx) ? 1 : 0;

    ui_menu_item_set_focus(ui_LabelOk, ui_LabelOk, ok_st, false);
    ui_menu_item_set_focus(ui_LabelBack, ui_LabelBack, back_st, false);

    // 4. Глобальные настройки панели
    ui_set_label_text(ui_LabelHeadEnter, _(text_labels[enter])); // или ваш источник текста

    ui_next_state.head_enter = false;
}

static __attribute__((unused)) void ui_update_input_panel_head_button(uint16_t enter)
{
    // Защита от выхода за пределы массива
    if (enter >= ROLLER_HEAD_END) return;

    const ui_button_cfg_t *cfg = &btn_table[enter];

    // 1. Текст с переводом
    ui_set_label_text(ui_LabelOk, _(cfg->ok_key));
    ui_set_label_text(ui_LabelBack, _(cfg->back_key));

    // 2. Геометрия
    ui_set_obj_size(ui_LabelOk, cfg->width, 44);
    ui_set_obj_size(ui_LabelBack, cfg->width, 44);

    // 3. Выравнивание и позиция
    ui_set_obj_align(ui_LabelOk, cfg->align_ok);
    ui_set_obj_align(ui_LabelBack, cfg->align_back);

    ui_set_obj_pos(ui_LabelOk, cfg->ok_x, cfg->ok_y);
    ui_set_obj_pos(ui_LabelBack, cfg->back_x, cfg->back_y);
}

//===========================================================================================================================
// Заполнение двух колонок:
// left_lbl  -> ВСЕ названия текущей страницы (с start_idx по start_idx + count_on_page)
// right_lbl -> ВСЕ значения текущей страницы (с start_idx по start_idx + count_on_page)
// Строки синхронизированы: i-я строка слева соответствует i-й строке справа.
//===========================================================================================================================
static char buf_left[400];  // Буфер побольше, чтобы влезло 9 длинных названий
static char buf_right[400]; // Буфер побольше для значений
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

//    if (item->category == 0) {
//        // Ошибка канала: "Порог 1, CO2"
//        if ((item->channel < COUNT_CHAN) && (item->error_bit < 10)) {
//            snprintf(out_buf, buf_size, "%s, %s",
//                    _(error_keys_chan[item->error_bit]),
//                    pSnsCfg->Sensor[item->channel].MolecularFormula);
//        }
//    }
//    else
    	if (item->category == 1) {
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
//    static uint32_t prev_chan_err[COUNT_CHAN] = {0};
    static uint32_t prev_sys_err = 0;
    uint8_t changed = 0;

    // 1. Проверяем каждый канал отдельно
//    for(uint8_t ch = 0; ch < COUNT_CHAN; ch++) {
//        uint32_t curr = pSnsCfg->Sensor[ch].StateErr & ERROR_MASK_ST_1;
//
//        // Сравниваем с предыдущим значением ЭТОГО канала
//        if(curr != prev_chan_err[ch]) {
//            changed = 1;
//            prev_chan_err[ch] = curr;  // Обновляем сохранённое значение
//        }
//    }

    // 2. Проверяем системные ошибки
    uint32_t curr_sys = pSnsCfg->Config_common.StateErr & ERROR_MASK_ST_2;
    if(curr_sys != prev_sys_err) {
        changed = 1;
        prev_sys_err = curr_sys;
    }

    // 3. Проверяем LORA
    #if CONFIG_LORA
    static uint8_t prev_lora = 0;
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

    // 1. Ошибки каналов todo по каналам не будет , надо убрать
//    for (uint8_t ch = 0; ch < COUNT_CHAN; ch++) {
//        if (TESTBIT(pSnsCfg->Sensor[ch].State, ST_BIT_CHANNEL_TURN_OFF)) continue;
//
//        uint32_t err_mask = pSnsCfg->Sensor[ch].StateErr & ERROR_MASK_ST_1;
//
//        for (uint8_t bit = 0; bit < 10; bit++) {
//            if ((TESTBIT(err_mask, bit)) && (error_queue_count < MAX_ERROR_QUEUE)) {
//                error_queue[error_queue_count].channel = ch;
//                error_queue[error_queue_count].category = 0;
//                error_queue[error_queue_count].error_bit = bit;
//                error_queue[error_queue_count].key = (0 << 8) | (ch << 4) | bit;
//                error_queue_count++;
//            }
//        }
//    }

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
//            lv_coord_t label_width = lv_obj_get_width(ui_LabelInfoStr);

            // 2. Вычисляем ширину текста в пикселях
//            lv_point_t txt_size;
//            lv_txt_get_size(&txt_size, current_error_text,
//                           lv_obj_get_style_text_font(ui_LabelInfoStr, LV_PART_MAIN),
//                           0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

            // Выбираем режим в зависимости от длины текста
//            lv_label_long_mode_t needed_mode = (txt_size.x > label_width) ?
//                                               LV_LABEL_LONG_SCROLL : LV_LABEL_LONG_CLIP;

            // Устанавливаем режим ТОЛЬКО если он изменился
//            ui_set_label_long_mode(ui_LabelInfoStr, needed_mode);

//            ui_set_label_text(ui_LabelInfoStr, current_error_text);
            ui_next_state.info_str = false;
        }
    }
    else {
//    	lv_label_set_long_mode(ui_LabelInfoStr, LV_LABEL_LONG_CLIP);
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

	lv_display_set_buffers(disp, buf_lcd, buf_lcd2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL); /* Set an initialized buffer */
	// 1. Инициализируем пакет переводов
	lv_i18n_init(lv_i18n_language_pack);

	// 2. Устанавливаем нужный язык (как в вашем .yml файле)
	lv_i18n_set_locale("ru-RU");

	ui_init();

	ui_ScreenHeadMain_screen_init();
	lv_obj_set_parent(ui_ImageMirax, lv_layer_top());
	lv_obj_set_parent(ui_PanelHead, lv_layer_top());
	lv_obj_set_parent(ui_PanelHeadEnter, lv_layer_top());
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
//		else if(act_scr == ui_ScreenSetting)      ui_ScreenSetting_screen_relocalize();
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

static __attribute__((unused)) void ui_set_obj_width(lv_obj_t * obj, int32_t w)
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
uint8_t state_rotation = 0;
uint8_t state_rotation_old = 0;
uint16_t LCD_Fill_Template(SNS_CFG *pSnsCfg)
{
	/* Здесь был static uint16_t last_tid = 255 - не читался ни разу (смену
	   экрана ниже определяет screen_was_changed), удалён: -Wunused-variable. */
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
		state_rotation_old = state_rotation;
	}

	LCD_Image_SET(0);


	LCD_Refresh_Language(APPLANG);

	//-------------------------------
	// 1. Скрытие панелей
	ui_reset_visibility_flags();
	//-------------------------------
	// 2. Инфо-панель

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
        /* Экран сменился: прежний уничтожен (scr_unloaded_delete_cb ->
           *_screen_destroy обнуляет указатели SquareLine), новый создан
           init_func'ом из scr_map. Кэши раскладок экрана измерений сбрасываем -
           иначе LCD_Measure() не станет выставлять геометрию заново (см.
           meas_reset_cache). Дёшево: сбрасываем только при смене экрана. */
        meas_reset_cache();
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
	SET_EXCHANGE(data);   /* была опечатка SET_EXCHANGE(data;) - лишняя ';' в аргументе */
	SET_LTE(data);
	SET_LTE_VEC(data);
}

//===========================================================================================================================
void ui_reset_visibility_flags(void)
{
    ui_next_state.info_str   	= true;
    ui_next_state.head_enter 	= true;
    ui_next_state.head_out   	= true;
    ui_next_state.head_timer 	= true;
}

//===========================================================================================================================
void ui_apply_visibility(void)
{
    // Благодаря нашей "умной" ui_set_obj_hidden,
    // LVGL дернется только если реальное состояние изменилось.
    ui_set_obj_hidden(ui_ImageMirax,     ui_next_state.mirax);
    ui_set_obj_hidden(ui_PanelHeadEnter, ui_next_state.head_enter);

    // Иконки
    ui_set_obj_hidden(ui_ImageLora,      ui_next_state.lora);
    ui_set_obj_hidden(ui_ImageGPS,       ui_next_state.gps);
    ui_set_obj_hidden(ui_ImageBLE,       ui_next_state.ble);
    ui_set_obj_hidden(ui_ImageExchange,    ui_next_state.exchange);

}

//===========================================================================================================================
void LCD_Menu(uint8_t num)
{
    // 1. Формируем список элементов в зависимости от модели
	lv_obj_t * labelsLeft[] = {ui_LabelLeft0, ui_LabelLeft1, ui_LabelLeft2, ui_LabelLeft3};
	lv_obj_t * labelsRight[] = {ui_LabelRight0, ui_LabelRight1, ui_LabelRight2, ui_LabelRight3};
	lv_obj_t * panels[] = {ui_Panel0, ui_Panel1, ui_Panel2, ui_Panel3};

    ui_set_obj_align(labelsLeft[0], LV_ALIGN_LEFT_MID);

    const uint8_t count = 4;

    for (uint8_t i = 0; i < count; i++) {
    	ui_set_obj_align(labelsLeft[i], LV_ALIGN_LEFT_MID);
        ui_menu_item_set_focus(labelsLeft[i], panels[i], (num == i), false);
        /* В дизайне SquareLine панели созданы СКРЫТЫМИ (LV_OBJ_FLAG_HIDDEN):
           показываем строки списка; правые лейблы в меню не используются -
           гасим (в дизайне там плейсхолдер "21.01.2014").                  */
        ui_set_obj_hidden(panels[i], false);
        ui_set_obj_hidden(labelsRight[i], true);
    }
}

//===========================================================================================================================
// ------------------------------------------------------------------
// Вариант 1: Одна панель (220px), ui_PanelHeadMenuUp скрыта
// ------------------------------------------------------------------
void LCD_SubMenu_Single(uint8_t num, uint16_t roller)
{
    if (roller >= ROLLER_END) return;

    // Текст на основной панели
    ui_set_obj_align(ui_LabelLeft0, LV_ALIGN_CENTER);
    ui_set_label_text(ui_LabelLeft0, _(text_labels[roller])); // или ваш источник текста
    /* Панель заголовка в дизайне скрыта - показываем (общий вход для
       Info/Setting: у них Panel0 = строка-заголовок подраздела).          */
    ui_set_obj_hidden(ui_Panel0, false);
	//-------------------------------
}

//===========================================================================================================================
void LCD_SubMenuInfo(SNS_CFG *pSnsCfg, uint8_t num)
{
    uint8_t current_page = num;
    LCD_SubMenu_Single(0, ROLLER_INFO);

    struct tm *tm_ptr = gmtime(&tmp_current);

    // Буферы для значений
    /* str_date[16], а не [12]: с приведением полей tm к их фактическим
       диапазонам (uint8_t/uint16_t) худший вывод "%02d.%02d.%04d" =
       3+1+3+1+5+1 = 14 байт. Без приведения GCC считает аргументы
       произвольными int (до 11 цифр) и ругается -Wformat-overflow/-truncation. */
    static char str_date[16], str_time[12];
    static char str_sn_dev[24];
    static char str_hw[12], str_fw[12];
    static char str_sys_err_buf[64];
    static char env_temp_str[15];
    static char env_press_str[15];
    static char env_hum_str[15];

    // Массивы указателей
    const char * labels[60];
    const char * values[60];
    uint8_t total_count = 0;

    // --- 1. Дата и Время (всегда отображаются) ---
    /* snprintf вместо sprintf (размер буфера известен) + аргументы в их реальных
       диапазонах: для gmtime() mday 1..31, mon 1..12, year 1970..2038, поэтому
       вывод всегда "ДД.ММ.ГГГГ" = 10 символов, усечение невозможно. */
    snprintf(str_date, sizeof(str_date), "%02d.%02d.%04d",
             (uint8_t)tm_ptr->tm_mday, (uint8_t)(tm_ptr->tm_mon + 1),
             (uint16_t)(tm_ptr->tm_year + 1900));
    labels[total_count] = _("Дата"); values[total_count++] = str_date;

    snprintf(str_time, sizeof(str_time), "%02d:%02d",
             (uint8_t)tm_ptr->tm_hour, (uint8_t)tm_ptr->tm_min);
    labels[total_count] = _("Время"); values[total_count++] = str_time;

    // --- 2. Заводской номер прибора (всегда отображается) ---
    // Инициализация флага формата на этапе компиляции
    uint8_t is_7_digits = 0; // 0: %08d, 1: %07d
    // Бренд - переменная рантайма (flash), в #if её сравнивать нельзя
    if (current_build_type == MIRAX)
    {
    #if ((DEVICE_NUMBER == Device2_Pro) || (DEVICE_NUMBER == Device2_Pro_1) || (DEVICE_NUMBER == Device3_Pro))
        is_7_digits = 1;
    #endif
    }
    uint32_t serial = ((uint32_t)pSnsCfg->Config_common.SerialHi << 16) | pSnsCfg->Config_common.SerialLo;
    char sn_buffer[24];
    strcpy(sn_buffer, (char*)SN_Label_Prefix[MIRAX_BACS_BUILD]);

    /* serial - uint32_t, поэтому %u и (unsigned): с %07d/%08d GCC ругался
       -Wformat, а номер >= 2^31 напечатался бы отрицательным. snprintf с
       остатком буфера (strlen считаем один раз) - sn_buffer[24] не переполнится. */
    size_t sn_len = strlen(sn_buffer);
    if (is_7_digits) {
        snprintf(sn_buffer + sn_len, sizeof(sn_buffer) - sn_len, "%07u", (unsigned)serial);
    } else {
        snprintf(sn_buffer + sn_len, sizeof(sn_buffer) - sn_len, "%08u", (unsigned)serial);
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
    /* Единицы sensors_status (BME280 int32-драйвер, см. Vector_main.h):
       Temperature = °C×100, Humidity = %×1024, Pressure = гПа×10.
       На экран - целые: °C, % и мм рт.ст. (гПа×10 × 0.0750062 = mmHg,
       10132 -> 760). Раньше значения печатались сырыми (×100/×1024).     */
    snprintf(env_temp_str, sizeof(env_temp_str), "%d °С",
             (int)(sensors_status.Temperature / 100.0f));
    labels[total_count] = env_labels[APPLANG][0]; values[total_count++] = env_temp_str;


    snprintf(env_press_str, sizeof(env_press_str), "%d mmHg",
             (int)(sensors_status.Pressure * 0.0750062f + 0.5f));
    labels[total_count] = env_labels[APPLANG][1]; values[total_count++] = env_press_str;


    snprintf(env_hum_str, sizeof(env_hum_str), "%d %%",
             (int)(sensors_status.Humidity / 1024.0f));
    labels[total_count] = env_labels[APPLANG][2]; values[total_count++] = env_hum_str;

    // --- 6. Расчет страниц ---
#if (CONFIG_MODEL_LCD == 0)
    const uint8_t LINES_PER_PAGE = 5;
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
    /* TODO автора ("Panel1 сделать height y = 25, у LabelLeft1 и LabelRight1
       align left top") выполнен в коде: высота строки INFO_LINE_HEIGHT = 25 px,
       высота Panel1 = число строк страницы × 25; лейблы прижаты к ВЕРХНЕМУ
       краю (имена - слева, значения - справа: оба TOP_LEFT наложились бы
       друг на друга - две колонки). Видимость: в дизайне SquareLine Panel1 и
       LabelRight1 созданы скрытыми - показываем; неиспользуемые на этой
       странице строки меню Panel2/Panel3 гасим (заголовок Panel0 показывает
       LCD_SubMenu_Single, лейблы - LCD_Fill_Info_Page).                    */
    ui_set_obj_hidden(ui_Panel1, false);
    ui_set_obj_hidden(ui_Panel2, true);
    ui_set_obj_hidden(ui_Panel3, true);
    ui_set_obj_align(ui_LabelLeft1, LV_ALIGN_TOP_LEFT);
    ui_set_obj_align(ui_LabelRight1, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_height(ui_Panel1, (int32_t)count_on_page * INFO_LINE_HEIGHT);

    LCD_Fill_Info_Page(ui_LabelLeft1, ui_LabelRight1, labels, values, start_idx, count_on_page);
}

//===========================================================================================================================
void LCD_SubMenuSetting(SNS_CFG *pSnsCfg, uint8_t num)
{
	//-------------------------------
	LCD_SubMenu_Single(0, ROLLER_SETTING);
	//-------------------------------
	uint8_t count_panels = 3;
	lv_obj_t * labelsLeft[] = {ui_LabelLeft1, ui_LabelLeft2, ui_LabelLeft3};
    lv_obj_t * panels[] = { ui_Panel1, ui_Panel2, ui_Panel3};


    for(int i = 0; i < count_panels; i++) {
    	ui_menu_item_set_focus(labelsLeft[i], panels[i], (num == i), 0);
		/* Панели строк в дизайне скрыты - показываем (как в LCD_Menu). */
		ui_set_obj_hidden(panels[i], false);
    }
}

//===========================================================================================================================
// ЭКРАН ИЗМЕРЕНИЙ (SCREEN_MEASURE -> ui_ScreenMeasurements)
//
// Панели в дизайне SquareLine ОДИНАКОВЫЕ: PanelMeasure (x=-79) и PanelMeasure1
// (x=+80), по 152x144, LV_ALIGN_CENTER в ContainerMeasure (311x144, y=6).
// Состав панели: ImageMeas (иконка), LabelMeas (значение, шрифт number85),
// LabelMeasName (подпись, number21), LabelMeasUnits (единицы, number21).
// В режимах «координаты / пульс / ошибки» ОДНА панель растягивается на весь
// контейнер (x=0, w=311), вторая гасится (ей передаётся MEAS_NONE).
//
// ДВА УРОВНЯ API (по просьбе автора: «чтобы выбиралось, в какую панель писать,
// какой режим и входные данные»):
//   LCD_Measure(panel, mode, value, name, units) - применить режим к ОДНОЙ
//       панели: геометрия/видимость/шрифты/цвет + текст.
//       panel: 0 = PanelMeasure (левая), 1 = PanelMeasure1 (правая);
//       mode : MEAS_* (TFT_indicator.h), MEAS_NONE = погасить панель;
//       строка NULL = текст этого элемента не менять (раскладка применяется в
//       любом случае - можно перерисовать геометрию, не трогая данные).
//   LCD_MeasureScreen(num) - экран ЦЕЛИКОМ: страница meas_pages[num] задаёт
//       режимы обеих панелей, значения берутся из данных прибора. Зовётся из
//       Menu_Draw() при SCREEN_MEASURE, num = menu_status.state_menu.
//
// Раскладка режимов - meas_layout[] («виды конфигурации» автора), соответствие
// «страница -> режимы панелей» - meas_pages[]: обе таблицы правятся в одном
// месте. Число страниц = VECTOR_MEASURE_PAGES (vector_config.h) - оно же задаёт
// предел скролла в menu_navigate() (case SCREEN_MEASURE).
//
// ВСЕ сеттеры - «умные» (ui_set_* в начале файла): свойство меняется, только
// если оно действительно изменилось. Геометрия/стили ставятся ДОПОЛНИТЕЛЬНО
// только при смене режима или пересоздании экрана (см. layout в LCD_Measure) -
// иначе lv_obj_set_pos() инвалидировал бы экран каждые 100 мс.
// КОНТЕКСТ: поток LVGL Task (Menu_Draw); из ISR/других потоков не вызывать.
//===========================================================================================================================
/* Геометрия панелей - из ui_ScreenMeasurements.c (экспорт SquareLine). */
#define MEAS_PANEL_H         (144)
#define MEAS_PANEL_W_HALF    (152)   /* половина контейнера: две панели рядом  */
#define MEAS_PANEL_W_FULL    (311)   /* панель на весь контейнер               */
#define MEAS_PANEL_X_LEFT    (-79)
#define MEAS_PANEL_X_RIGHT   (80)
#define MEAS_PANEL_X_FULL    (0)

/* Выравнивание элементов - из дизайна SquareLine (ui_ScreenMeasurements.c):
   ImageMeas/LabelMeasName/LabelMeasUnits - LV_ALIGN_TOP_MID, LabelMeas -
   LV_ALIGN_CENTER. Смещения x/y в раскладке ниже задаются ОТНОСИТЕЛЬНО этих
   точек выравнивания (как в описании автора), поэтому менять align не нужно. */

/* Размеры строк (статические буферы, по одному на панель). */
#define MEAS_VALUE_SZ        (16u)   /* "101.3", "23.5", "-12", "7"            */
#define MEAS_NAME_SZ         (48u)   /* "55.678490 N" или текст ошибки (current_error_text[40]) */
#define MEAS_UNITS_SZ        (24u)   /* "°C", "%", "кПа", вторая координата    */

/* Заглушка для режимов, у которых в проекте пока нет источника данных (решение
   автора): на экране её видно, и она не путается с настоящим нулём. */
#define MEAS_NO_DATA         "--"

/* Число шрифтов для автоподбора значения. number85 = 48 px на цифру (adv_w
   766/16), поэтому в половину панели (152 px минус паддинги темы) влезает
   только ТРИ знака - ровно как "143" в дизайне. "23.5" (4 знака = 165 px) и
   "101.3" (5 знаков = 213 px) LVGL обрезал бы по границе панели (дочерние
   объекты режутся, если не выставлен LV_OBJ_FLAG_OVERFLOW_VISIBLE), поэтому
   значение, которое не влезает, печатаем шрифтом мельче: number36 (~20 px на
   цифру), затем number21 (~12 px). Отключается VECTOR_LCD_MEASURE_AUTOFIT 0
   (vector_config.h) - тогда всегда number85. В ui.h объявлены только
   number85/36/21/14/9: файлы number11/16/18 в экспорт SquareLine не попали. */
#define MEAS_VALUE_FONT_N    (3u)

//------------------------------------------------------------------------------
/* Раскладка ОДНОГО режима (координаты/видимость/шрифты - по описанию автора). */
typedef struct
{
	uint8_t      full_width;    /* 1 = панель на весь контейнер (x=0, w=311)   */
	uint8_t      img_visible;   /* ImageMeas                                   */
	uint8_t      val_visible;   /* LabelMeas      (значение)                   */
	uint8_t      name_visible;  /* LabelMeasName  (подпись)                    */
	uint8_t      units_visible; /* LabelMeasUnits (единицы)                    */
	int16_t      img_x,   img_y;    lv_align_t img_align;
	int16_t      val_x,   val_y;    lv_align_t val_align;
	int16_t      name_x,  name_y;   lv_align_t name_align;
	int16_t      units_x, units_y;  lv_align_t units_align;
	const lv_font_t * name_font;    /* шрифт подписи (number21 / number36)     */
	const lv_font_t * units_font;   /* шрифт единиц                            */
	uint8_t      val_color2;    /* 1 = цвет значения textcolor2 (зелёный),
	                               0 = textcolor (чёрный)                      */
	uint8_t      name_line;     /* 1 = подпись во всю ширину панели, текст
	                               влево, длинная строка прокручивается
	                               (режим «Ошибки»); 0 = ширина по содержимому */
	const void  *img_src;       /* иконка режима; NULL = текущую не менять     */
} meas_layout_t;

/* Виды конфигурации автора. Порядок полей - см. meas_layout_t; незаполненные
   поля = 0/NULL (C99 designated initializers). */
static const meas_layout_t meas_layout[MEAS_MODE_END] =
{
	/* ничего не показывать: панель гасится целиком (вторая заняла весь экран) */
	[MEAS_NONE] = {
		.name_font = &ui_font_number21, .units_font = &ui_font_number21 },

	/* Первоначальная настройка (база для остальных режимов):
	   LabelMeas(0,30) ImageMeas(55,-5) LabelMeasName(25,0) LabelMeasUnits(50,25) */
	[MEAS_DEFAULT] = {
		.img_visible = 1, .val_visible = 1, .name_visible = 1, .units_visible = 1,
		.img_x = 55, .img_y = -5, .img_align = LV_ALIGN_TOP_MID,
		.val_x = 0, .val_y = 30, .val_align = LV_ALIGN_CENTER,
		.name_x = 25, .name_y = 0, .name_align = LV_ALIGN_TOP_MID,
		.units_x = 50, .units_y = 25, .units_align = LV_ALIGN_TOP_MID,
		.name_font = &ui_font_number21, .units_font = &ui_font_number21,
		.img_src = NULL },

	/* Температура: LabelMeasName(35,0) */
	[MEAS_TEMP] = {
		.img_visible = 1, .val_visible = 1, .name_visible = 1, .units_visible = 1,
		.img_x = 55, .img_y = -5, .img_align = LV_ALIGN_TOP_MID,
		.val_x = 0, .val_y = 30, .val_align = LV_ALIGN_CENTER,
		.name_x = 35, .name_y = 0, .name_align = LV_ALIGN_TOP_MID,
		.units_x = 50, .units_y = 25, .units_align = LV_ALIGN_TOP_MID,
		.name_font = &ui_font_number21, .units_font = &ui_font_number21,
		.img_src = &ui_img_imagemenu_temp_png },

	/* Влажность: LabelMeasName(35,0) */
	[MEAS_HUM] = {
		.img_visible = 1, .val_visible = 1, .name_visible = 1, .units_visible = 1,
		.img_x = 55, .img_y = -5, .img_align = LV_ALIGN_TOP_MID,
		.val_x = 0, .val_y = 30, .val_align = LV_ALIGN_CENTER,
		.name_x = 35, .name_y = 0, .name_align = LV_ALIGN_TOP_MID,
		.units_x = 50, .units_y = 25, .units_align = LV_ALIGN_TOP_MID,
		.name_font = &ui_font_number21, .units_font = &ui_font_number21,
		.img_src = &ui_img_imagemenu_humidity_png },

	/* Давление: LabelMeasName(25,0) */
	[MEAS_PRESS] = {
		.img_visible = 1, .val_visible = 1, .name_visible = 1, .units_visible = 1,
		.img_x = 55, .img_y = -5, .img_align = LV_ALIGN_TOP_MID,
		.val_x = 0, .val_y = 30, .val_align = LV_ALIGN_CENTER,
		.name_x = 25, .name_y = 0, .name_align = LV_ALIGN_TOP_MID,
		.units_x = 50, .units_y = 25, .units_align = LV_ALIGN_TOP_MID,
		.name_font = &ui_font_number21, .units_font = &ui_font_number21,
		.img_src = &ui_img_imagemenu_pressure_png },

	/* Координаты: панель на весь экран; ImageMeas и LabelMeas скрыты;
	   LabelMeasName(0,15) и LabelMeasUnits(0,60) - обе строки шрифтом number36
	   (широта в подписи, долгота в «единицах») */
	[MEAS_COORD] = {
		.full_width = 1,
		.name_visible = 1, .units_visible = 1,
		.img_align = LV_ALIGN_TOP_MID,
		.val_x = 0, .val_y = 30, .val_align = LV_ALIGN_CENTER,
		.name_x = 0, .name_y = 15, .name_align = LV_ALIGN_TOP_MID,
		.units_x = 0, .units_y = 60, .units_align = LV_ALIGN_TOP_MID,
		.name_font = &ui_font_number36, .units_font = &ui_font_number36,
		.img_src = NULL },

	/* AQI (индекс качества воздуха): LabelMeas(0,30) цветом textcolor2
	   (зелёный), ImageMeas и LabelMeasUnits скрыты */
	[MEAS_AQI] = {
		.val_visible = 1, .name_visible = 1,
		.img_align = LV_ALIGN_TOP_MID,
		.val_x = 0, .val_y = 30, .val_align = LV_ALIGN_CENTER,
		.name_x = 25, .name_y = 0, .name_align = LV_ALIGN_TOP_MID,
		.units_x = 50, .units_y = 25, .units_align = LV_ALIGN_TOP_MID,
		.name_font = &ui_font_number21, .units_font = &ui_font_number21,
		.val_color2 = 1, .img_src = NULL },

	/* Пульс: панель на весь экран; LabelMeasUnits скрыт;
	   LabelMeasName(115,25) ImageMeas(130,0) LabelMeas(0,0) цвет textcolor */
	[MEAS_HEART] = {
		.full_width = 1,
		.img_visible = 1, .val_visible = 1, .name_visible = 1,
		.img_x = 130, .img_y = 0, .img_align = LV_ALIGN_TOP_MID,
		.val_x = 0, .val_y = 0, .val_align = LV_ALIGN_CENTER,
		.name_x = 115, .name_y = 25, .name_align = LV_ALIGN_TOP_MID,
		.units_x = 50, .units_y = 25, .units_align = LV_ALIGN_TOP_MID,
		.name_font = &ui_font_number21, .units_font = &ui_font_number21,
		.img_src = &ui_img_imagemenu_heart_png },

	/* Голосовые сообщения: подписи скрыты, иконка по центру панели */
	[MEAS_VOICE] = {
		.img_visible = 1,
		.img_x = 0, .img_y = 0, .img_align = LV_ALIGN_CENTER,
		.val_x = 0, .val_y = 30, .val_align = LV_ALIGN_CENTER,
		.name_x = 25, .name_y = 0, .name_align = LV_ALIGN_TOP_MID,
		.units_x = 50, .units_y = 25, .units_align = LV_ALIGN_TOP_MID,
		.name_font = &ui_font_number21, .units_font = &ui_font_number21,
		.img_src = &ui_img_imagemenu_voice_png },

	/* Чат (текстовые сообщения): подписи скрыты, иконка по центру панели */
	[MEAS_CHAT] = {
		.img_visible = 1,
		.img_x = 0, .img_y = 0, .img_align = LV_ALIGN_CENTER,
		.val_x = 0, .val_y = 30, .val_align = LV_ALIGN_CENTER,
		.name_x = 25, .name_y = 0, .name_align = LV_ALIGN_TOP_MID,
		.units_x = 50, .units_y = 25, .units_align = LV_ALIGN_TOP_MID,
		.name_font = &ui_font_number21, .units_font = &ui_font_number21,
		.img_src = &ui_img_imagemenu_chat_png },

	/* RSSI: LabelMeasUnits скрыт; LabelMeasName(-40,10) играет роль единиц
	   («дБ•Гц»), значение - в LabelMeas(0,30) */
	[MEAS_RSSI] = {
		.img_visible = 1, .val_visible = 1, .name_visible = 1,
		.img_x = 55, .img_y = -5, .img_align = LV_ALIGN_TOP_MID,
		.val_x = 0, .val_y = 30, .val_align = LV_ALIGN_CENTER,
		.name_x = -40, .name_y = 10, .name_align = LV_ALIGN_TOP_MID,
		.units_x = 50, .units_y = 25, .units_align = LV_ALIGN_TOP_MID,
		.name_font = &ui_font_number21, .units_font = &ui_font_number21,
		.img_src = &ui_img_imagemenu_ltebig_png },

	/* Количество спутников: LabelMeasUnits скрыт, число - в LabelMeas(0,30) */
	[MEAS_SAT] = {
		.img_visible = 1, .val_visible = 1, .name_visible = 1,
		.img_x = 55, .img_y = -5, .img_align = LV_ALIGN_TOP_MID,
		.val_x = 0, .val_y = 30, .val_align = LV_ALIGN_CENTER,
		.name_x = 25, .name_y = 0, .name_align = LV_ALIGN_TOP_MID,
		.units_x = 50, .units_y = 25, .units_align = LV_ALIGN_TOP_MID,
		.name_font = &ui_font_number21, .units_font = &ui_font_number21,
		.img_src = &ui_img_imagemenu_satellite_png },

	/* Ошибки: панель на весь экран; ImageMeas/LabelMeas/LabelMeasUnits скрыты;
	   LabelMeasName(0,15) number21, текст ВЛЕВО и во всю ширину панели (длинная
	   строка прокручивается) - сюда встаёт текст из LCD_UpdateErrorDisplay */
	[MEAS_ERR] = {
		.full_width = 1,
		.name_visible = 1,
		.img_align = LV_ALIGN_TOP_MID,
		.val_x = 0, .val_y = 30, .val_align = LV_ALIGN_CENTER,
		.name_x = 0, .name_y = 15, .name_align = LV_ALIGN_TOP_MID,
		.units_x = 50, .units_y = 25, .units_align = LV_ALIGN_TOP_MID,
		.name_font = &ui_font_number21, .units_font = &ui_font_number21,
		.name_line = 1, .img_src = NULL },
};

//------------------------------------------------------------------------------
/* Объекты панели. Храним указатели НА ПЕРЕМЕННЫЕ SquareLine (ui_PanelMeasure и
   т.д.), а не их значения: ui_ScreenMeasurements_screen_init() перезаписывает
   их при создании экрана, а ui_ScreenMeasurements_screen_destroy() (зовётся из
   scr_unloaded_delete_cb при уходе с экрана) - обнуляет. Поэтому разыменовываем
   перед каждым использованием и проверяем NULL. */
typedef struct
{
	lv_obj_t ** panel;
	lv_obj_t ** img;
	lv_obj_t ** val;
	lv_obj_t ** name;
	lv_obj_t ** units;
} meas_panel_t;

static const meas_panel_t meas_panel[2] =
{
	{ &ui_PanelMeasure,  &ui_ImageMeas,  &ui_LabelMeas,  &ui_LabelMeasName,  &ui_LabelMeasUnits  },
	{ &ui_PanelMeasure1, &ui_ImageMeas1, &ui_LabelMeas1, &ui_LabelMeasName1, &ui_LabelMeasUnits1 },
};

/* Кэш на панель: последний режим и указатель панели, плюс последний текст
   значения (чтобы не подбирать шрифт каждый проход). Сбрасывает их
   meas_reset_cache() - см. вызов в LCD_Fill_Template(). */
static uint8_t   meas_mode_cache[2] = { (uint8_t)MEAS_MODE_END, (uint8_t)MEAS_MODE_END };
static lv_obj_t *meas_pnl_cache[2]  = { NULL, NULL };
static char      meas_value_cache[2][MEAS_VALUE_SZ];

/* Экран ПЕРЕСОЗДАН: кэши раскладок надо сбросить, иначе LCD_Measure() решит,
   что геометрия уже выставлена, а объекты на самом деле свежие - со значениями
   из дизайна SquareLine (панель 152x144, "143"/"H"/"°C", шрифт number85, цвет
   textcolor2). Сравнения указателя панели для этого НЕДОСТАТОЧНО: уничтоженный
   объект и созданный на его месте очень часто получают ОДИН И ТОТ ЖЕ адрес
   (куча LVGL возвращает тот же блок на тот же размер), т.е. указатель «совпадёт».
   Достоверный признак - смена экрана в LCD_Fill_Template(). Вызов - поток LVGL
   Task, там же, где и отрисовка. */
static void meas_reset_cache(void)
{
	uint8_t i;
	for (i = 0u; i < 2u; i++)
	{
		meas_mode_cache[i]     = (uint8_t)MEAS_MODE_END;
		meas_pnl_cache[i]      = NULL;
		meas_value_cache[i][0] = '\0';
	}
}

/* Страницы экрана измерений: state_menu (0..VECTOR_MEASURE_PAGES-1) -> режим
   левой и правой панелей; MEAS_NONE = панель скрыта (её место занял режим с
   full_width). Раскладка согласована с menu_navigate(): LONG Button3 на
   странице 5 уходит в SCREEN_VOICE_MESSAGE, на странице 6 - в
   SCREEN_TEXT_MESSAGE (ветка ==6 была недостижима, пока скролл стоял на 6
   страницах 0..5; теперь предел = VECTOR_MEASURE_PAGES).
   MEAS_ERR в ротацию НЕ включён: строку ошибок логичнее показывать по событию
   (или подменять ею страницу при error_queue_count != 0) - решение за автором. */
static const uint8_t meas_pages[VECTOR_MEASURE_PAGES][2] =
{
	{ (uint8_t)MEAS_TEMP,  (uint8_t)MEAS_HUM   },   /* 0 температура | влажность */
	{ (uint8_t)MEAS_PRESS, (uint8_t)MEAS_AQI   },   /* 1 давление    | AQI       */
	{ (uint8_t)MEAS_COORD, (uint8_t)MEAS_NONE  },   /* 2 координаты (весь экран) */
	{ (uint8_t)MEAS_SAT,   (uint8_t)MEAS_RSSI  },   /* 3 спутники    | RSSI      */
	{ (uint8_t)MEAS_HEART, (uint8_t)MEAS_NONE  },   /* 4 пульс (весь экран)      */
	{ (uint8_t)MEAS_VOICE, (uint8_t)MEAS_NONE  },   /* 5 голосовые -> LONG B3    */
	{ (uint8_t)MEAS_CHAT,  (uint8_t)MEAS_NONE  },   /* 6 чат       -> LONG B3    */
};
typedef char meas_pages_assert[(sizeof(meas_pages) / sizeof(meas_pages[0]) == (size_t)VECTOR_MEASURE_PAGES) ? 1 : -1];

//===========================================================================================================================
// Форматирование значений
//===========================================================================================================================
/* Округление float к целому без lroundf() (не тянем libm). */
static int32_t meas_f2i(float v)
{
	return (int32_t)((v >= 0.0f) ? (v + 0.5f) : (v - 0.5f));
}

/* Число с одним знаком после точки из значения x10: 235 -> "23.5", -53 ->
   "-5.3", 1013 -> "101.3". Знак печатаем сами, чтобы -0.5 не стало "0.-5". */
static void meas_fmt_x10(char *buf, uint32_t sz, int32_t v10)
{
	int32_t a = (v10 < 0) ? -v10 : v10;
	snprintf(buf, sz, "%s%d.%u", (v10 < 0) ? "-" : "", (int)(a / 10), (unsigned)(a % 10));
}

/* Целое число: 7 -> "7". */
static void meas_fmt_int(char *buf, uint32_t sz, int32_t v)
{
	snprintf(buf, sz, "%d", (int)v);
}

/* Координата из gps_nav.lat_scaled/lon_scaled (градусы x1e-7) в виде
   "55.678490 N" (формат автора: 6 знаков после точки + полушарие). */
static void meas_fmt_coord(char *buf, uint32_t sz, int32_t scaled, char pos_hemi, char neg_hemi)
{
	int32_t a    = (scaled < 0) ? -scaled : scaled;
	int32_t deg  = a / 10000000;              /* целые градусы                  */
	int32_t frac = (a % 10000000) / 10;       /* 6 знаков после точки           */
	snprintf(buf, sz, "%d.%06ld %c", (int)deg, (long)frac,
			 (scaled < 0) ? neg_hemi : pos_hemi);
}

/* Текст значения в LabelMeas + автоподбор шрифта (см. MEAS_VALUE_FONT_N).
   Подбираем только при изменении текста, а не каждый проход (100 мс):
   lv_text_get_size() считает ширину по глифам.
   avail - ширина панели из раскладки (см. panel_w в LCD_Measure): дети
   обрезаются по ГРАНИЦЕ родителя (lv_refr.c: clip_area = obj->coords), а не по
   его области содержимого, поэтому запас - полная ширина (152 / 311). Штатное
   "143" из дизайна = 144 px и влезает впритык; выравнивание же считается от
   области содержимого (lv_obj_refr_pos: pw = lv_obj_get_content_width(parent) =
   152 - 2*13, тема lv_theme_default кладёт pad_all = LV_DPX(16) = 13 px), но у
   значения x = 0 и LV_ALIGN_CENTER, поэтому оно в любом случае по центру. */
static void meas_set_value(uint8_t panel, lv_obj_t *val, int32_t avail, const char *text)
{
	if ((val == NULL) || (text == NULL)) { return; }
	if (strcmp(meas_value_cache[panel], text) == 0) { return; }   /* текст тот же - шрифт уже подобран */

#if VECTOR_LCD_MEASURE_AUTOFIT
	{
		static const lv_font_t *const value_fonts[MEAS_VALUE_FONT_N] =
			{ &ui_font_number85, &ui_font_number36, &ui_font_number21 };
		lv_point_t sz;
		uint8_t    i;

		for (i = 0u; i < MEAS_VALUE_FONT_N; i++)
		{
			lv_text_get_size(&sz, text, value_fonts[i], 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
			if ((sz.x <= avail) || (i == (uint8_t)(MEAS_VALUE_FONT_N - 1u))) { break; }
		}
		ui_set_obj_font(val, value_fonts[i], LV_PART_MAIN);
	}
#endif
	ui_set_label_text(val, text);
	snprintf(meas_value_cache[panel], MEAS_VALUE_SZ, "%s", text);
}

//===========================================================================================================================
// Режим ОДНОЙ панели: раскладка из meas_layout[mode] + текст.
// panel: 0 = PanelMeasure (левая, x=-79), 1 = PanelMeasure1 (правая, x=+80)
// mode : MEAS_* (TFT_indicator.h); MEAS_NONE - погасить панель
// value/name/units: готовые строки; NULL = текст элемента не менять
// КОНТЕКСТ: поток LVGL Task (Menu_Draw -> LCD_MeasureScreen).
//===========================================================================================================================
void LCD_Measure(uint8_t panel, uint8_t mode, const char *value, const char *name, const char *units)
{
	const meas_layout_t *L;
	lv_obj_t *pnl, *img, *val, *nm, *un;
	uint8_t   layout = 0u;   /* 1 = ставить геометрию/стили (сменился режим или экран пересоздан) */
	int32_t   panel_w;       /* ширина панели из раскладки (152 или 311)      */

	if ((panel > 1u) || (mode >= (uint8_t)MEAS_MODE_END)) { return; }
	L = &meas_layout[mode];

	/* Ширину берём ИЗ РАСКЛАДКИ, а не lv_obj_get_width(pnl): obj->coords
	   пересчитываются только на следующем проходе layout (lv_obj_update_layout
	   зовётся из lv_timer_handler), т.е. сразу после lv_obj_set_size() геттер
	   вернул бы СТАРЫЙ размер. */
	panel_w = (L->full_width != 0u) ? MEAS_PANEL_W_FULL : MEAS_PANEL_W_HALF;

	pnl = *(meas_panel[panel].panel);
	img = *(meas_panel[panel].img);
	val = *(meas_panel[panel].val);
	nm  = *(meas_panel[panel].name);
	un  = *(meas_panel[panel].units);

	/* Экран измерений ещё не создан (ui_ScreenMeasurements_screen_init() зовёт
	   LCD_Fill_Template при первом переходе на SCREEN_MEASURE) - рисовать нечем. */
	if (pnl == NULL) { return; }

	/* Панель в этой странице не участвует (вторая заняла весь контейнер). */
	if (mode == (uint8_t)MEAS_NONE)
	{
		ui_set_obj_hidden(pnl, true);
		return;
	}

	/* Геометрия и стили зависят ТОЛЬКО от режима, поэтому ставим их при смене
	   режима ИЛИ когда экран пересоздан (SquareLine уничтожает экран при уходе:
	   scr_unloaded_delete_cb -> ui_ScreenMeasurements_screen_destroy обнуляет
	   указатели, при возврате screen_init создаёт объекты заново; главный
	   признак пересоздания - meas_reset_cache() из LCD_Fill_Template, сравнение
	   указателя панели ниже - лишь подстраховка, адреса в куче LVGL повторяются).
	   Каждый проход это делать НЕЛЬЗЯ: lv_obj_get_x() возвращает вычисленную
	   координату, а не x из стиля, поэтому «умный» ui_set_obj_pos не отличит
	   «уже стоит» от «надо поставить» и будет звать lv_obj_set_pos() ->
	   lv_obj_refresh_style() -> инвалидацию экрана каждые 100 мс. */
	if ((meas_mode_cache[panel] != mode) || (meas_pnl_cache[panel] != pnl))
	{
		meas_mode_cache[panel]     = mode;
		meas_pnl_cache[panel]      = pnl;
		meas_value_cache[panel][0] = '\0';   /* шрифт значения подобрать заново */
		layout = 1u;
	}

	ui_set_obj_hidden(pnl, false);

	if (layout != 0u)
	{
		/* Панель: половина контейнера или весь экран. */
		ui_set_obj_size(pnl, panel_w, MEAS_PANEL_H);
		ui_set_obj_pos(pnl, (L->full_width != 0u) ? MEAS_PANEL_X_FULL
							: ((panel == 0u) ? MEAS_PANEL_X_LEFT : MEAS_PANEL_X_RIGHT), 0);

		/* Иконка режима. */
		if (img != NULL)
		{
			if (L->img_src != NULL) { ui_set_img_src(img, L->img_src); }
			ui_set_obj_align(img, L->img_align);
			ui_set_obj_pos(img, L->img_x, L->img_y);
		}
	}
	ui_set_obj_hidden(img, (L->img_visible == 0u));

	/* Значение (LabelMeas). */
	if ((layout != 0u) && (val != NULL))
	{
		ui_set_obj_align(val, L->val_align);
		ui_set_obj_pos(val, L->val_x, L->val_y);
		/* Цвет берём из палитры темы (textcolor - чёрный 0x000000, textcolor2 -
		   зелёный 0x43CD91), а не константой: смена темы (ui_theme_idx)
		   продолжит работать. Массив темы сам распадается в указатель, поэтому
		   «&_ui_theme_color_*» здесь было бы указателем НА МАССИВ. */
		const ui_theme_variable_t *theme_color = (L->val_color2 != 0u)
				? _ui_theme_color_textcolor2 : _ui_theme_color_textcolor;
		ui_set_obj_text_color(val,
				lv_color_hex((uint32_t)ui_get_theme_value(theme_color)),
				LV_PART_MAIN);
	}
	ui_set_obj_hidden(val, (L->val_visible == 0u));
	if (L->val_visible != 0u) { meas_set_value(panel, val, panel_w, value); }

	/* Подпись (LabelMeasName): имя величины, единицы (RSSI) или строка ошибки. */
	if ((layout != 0u) && (nm != NULL))
	{
		ui_set_obj_align(nm, L->name_align);
		ui_set_obj_pos(nm, L->name_x, L->name_y);
		ui_set_obj_font(nm, L->name_font, LV_PART_MAIN);
		/* «Ошибки»: строка во всю ширину панели (минус паддинги темы), длинный
		   текст прокручивается; иначе ширина по содержимому - как в дизайне
		   SquareLine. Ставим только при смене раскладки: LV_SIZE_CONTENT с
		   фактической шириной не сравнивается, и ui_set_obj_width дёргал бы
		   lv_obj_set_width() зря. Паддинги читаем из СТИЛЕЙ (они свежи всегда),
		   а не через lv_obj_get_content_width() - см. panel_w. */
		if (L->name_line != 0u)
		{
			lv_obj_set_width(nm, panel_w - lv_obj_get_style_pad_left(pnl, LV_PART_MAIN)
										  - lv_obj_get_style_pad_right(pnl, LV_PART_MAIN));
		}
		else
		{
			lv_obj_set_width(nm, LV_SIZE_CONTENT);
		}
	}
	ui_set_obj_hidden(nm, (L->name_visible == 0u));
	if (nm != NULL)
	{
		ui_set_label_long_mode(nm, (L->name_line != 0u) ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_WRAP);
		if (name != NULL) { ui_set_label_text(nm, name); }
	}

	/* Единицы (LabelMeasUnits). */
	if ((layout != 0u) && (un != NULL))
	{
		ui_set_obj_align(un, L->units_align);
		ui_set_obj_pos(un, L->units_x, L->units_y);
		ui_set_obj_font(un, L->units_font, LV_PART_MAIN);
	}
	ui_set_obj_hidden(un, (L->units_visible == 0u));
	if ((L->units_visible != 0u) && (un != NULL) && (units != NULL))
	{
		ui_set_label_text(un, units);
	}
}

//===========================================================================================================================
// Наполнение строк режима данными прибора.
// Что в проекте УЖЕ есть:
//   температура/влажность/давление - sensors_status (BME280, CONFIG_BME):
//       Temperature = °C×100, Humidity = %×1024, Pressure = гПа×10 (= кПа×100),
//       Sensors_ok & SENSORS_OK_BME = датчик ответил при инициализации;
//   координаты/спутники/сигнал     - gps_nav (CONFIG_GPS): lat/lon_scaled =
//       градусы×1e-7, satellites_used, navigation (1 = фикс есть),
//       signal_db_hz = C/No «золотой четвёрки» (решение автора: уровень
//       сигнала на экране измерений берём отсюда, единицы дБ-Гц, не дБм);
//   ошибки                         - current_error_text[40] (заполняет
//       LCD_UpdateErrorDisplay() этого файла, вызов - в Menu_Draw);
//       режим MEAS_ERR в ротацию страниц не входит - вызывается напрямую.
// Источников ПОКА НЕТ (решение автора - заглушки) -> MEAS_NO_DATA:
//   AQI    - датчика качества воздуха нет (в LoRa-журнале поле aqi - задел);
//   пульс  - API Lora_SetPulseData() есть, данных с пульсометра нет;
//   голосовые/чат - CONFIG_LORA = 0 (текстовые сообщения: Lora_GetMessage()).
// Если понадобится RSSI в дБм (а не C/No от GPS) - кандидаты:
//   Ble_telemetry.beacons[].Rssi / ppe[].Rssi (модуль MK01, CONFIG_BLE),
//   RSSI LoRa-модуля (CONFIG_LORA); правка - только в case MEAS_RSSI ниже.
// Когда источник появится - правится ТОЛЬКО этот switch.
//===========================================================================================================================
static void meas_fill(uint8_t mode, char *value, char *name, char *units)
{
	value[0] = '\0';
	name[0]  = '\0';
	units[0] = '\0';

	switch (mode)
	{
	case MEAS_TEMP:
		/* °C×100 -> ×10: один знак после точки (решение автора) */
		if ((sensors_status.Sensors_ok & SENSORS_OK_BME) != 0u)
		{
			meas_fmt_x10(value, MEAS_VALUE_SZ, meas_f2i(sensors_status.Temperature / 10.0f));
		}
		else { snprintf(value, MEAS_VALUE_SZ, "%s", MEAS_NO_DATA); }
		snprintf(name,  MEAS_NAME_SZ,  "%s", "T");
		snprintf(units, MEAS_UNITS_SZ, "%s", "°C");
		break;

	case MEAS_HUM:
		/* %×1024 -> целые % */
		if ((sensors_status.Sensors_ok & SENSORS_OK_BME) != 0u)
		{
			meas_fmt_int(value, MEAS_VALUE_SZ, meas_f2i(sensors_status.Humidity / 1024.0f));
		}
		else { snprintf(value, MEAS_VALUE_SZ, "%s", MEAS_NO_DATA); }
		snprintf(name,  MEAS_NAME_SZ,  "%s", "H");
		snprintf(units, MEAS_UNITS_SZ, "%s", "%");
		break;

	case MEAS_PRESS:
		/* гПа×10 = кПа×100 -> ×10: "101.3" (единицы кПа, решение автора) */
		if ((sensors_status.Sensors_ok & SENSORS_OK_BME) != 0u)
		{
			meas_fmt_x10(value, MEAS_VALUE_SZ, meas_f2i(sensors_status.Pressure / 10.0f));
		}
		else { snprintf(value, MEAS_VALUE_SZ, "%s", MEAS_NO_DATA); }
		snprintf(name,  MEAS_NAME_SZ,  "%s", "P");
		snprintf(units, MEAS_UNITS_SZ, "%s", "кПа");
		break;

	case MEAS_COORD:
		/* LabelMeas скрыт: широта - в подписи, долгота - в «единицах» (обе
		   строки шрифтом number36). Нет фикса - заглушка в обеих строках. */
#if CONFIG_GPS
		if (gps_nav.navigation != 0u)
		{
			meas_fmt_coord(name,  MEAS_NAME_SZ,  gps_nav.lat_scaled, 'N', 'S');
			meas_fmt_coord(units, MEAS_UNITS_SZ, gps_nav.lon_scaled, 'E', 'W');
			break;
		}
#endif /* CONFIG_GPS */
		snprintf(name,  MEAS_NAME_SZ,  "%s", MEAS_NO_DATA);
		snprintf(units, MEAS_UNITS_SZ, "%s", MEAS_NO_DATA);
		break;

	case MEAS_AQI:
		snprintf(value, MEAS_VALUE_SZ, "%s", MEAS_NO_DATA);   /* датчика AQI нет */
		snprintf(name,  MEAS_NAME_SZ,  "%s", "AQI");
		break;

	case MEAS_HEART:
		snprintf(value, MEAS_VALUE_SZ, "%s", MEAS_NO_DATA);   /* пульсометра нет */
		snprintf(name,  MEAS_NAME_SZ,  "%s", "bpm");
		break;

	case MEAS_RSSI:
		/* Уровень сигнала - C/No от GPS (решение автора): gps_nav.signal_db_hz,
		   среднее четырёх лучших спутников из GSV, один знак после точки;
		   0 = данных нет (нет фикса/модуль молчит) -> заглушка. Единицы дБ-Гц,
		   НЕ дБм. В подписи bullet «•» (U+2022) вместо средней точки «·»:
		   U+00B7 в number21/number36 отсутствует (диапазоны шрифта 0x20-0x7F,
		   0xB0, 0x2022, 0x410-0x44F, 0x451), а U+2022 - есть. «42.5» в
		   number85 = 167 px > 152, поэтому значение уедет в number36
		   (автоподбор); подпись «дБ•Гц» = 56 px при x=-40 -> 8..64, влезает. */
		snprintf(name, MEAS_NAME_SZ, "%s", "дБ•Гц");
#if CONFIG_GPS
		if (gps_nav.signal_db_hz > 0.0f)
		{
			meas_fmt_x10(value, MEAS_VALUE_SZ, meas_f2i(gps_nav.signal_db_hz * 10.0f));
			break;
		}
#endif /* CONFIG_GPS */
		snprintf(value, MEAS_VALUE_SZ, "%s", MEAS_NO_DATA);
		break;

	case MEAS_SAT:
#if CONFIG_GPS
		meas_fmt_int(value, MEAS_VALUE_SZ, (int32_t)gps_nav.satellites_used);
#else
		snprintf(value, MEAS_VALUE_SZ, "%s", MEAS_NO_DATA);
#endif /* CONFIG_GPS */
		snprintf(name,  MEAS_NAME_SZ,  "%s", "Спутники");
		break;

	case MEAS_ERR:
		/* Текст текущей ошибки: LCD_UpdateErrorDisplay() ротует очередь ошибок
		   (error_queue) раз в секунду по фазе мигания. Ошибок нет - пишем. */
		if ((error_queue_count != 0u) && (current_error_text[0] != '\0'))
		{
			snprintf(name, MEAS_NAME_SZ, "%s", current_error_text);
		}
		else
		{
			snprintf(name, MEAS_NAME_SZ, "%s", "Ошибок нет");
		}
		break;

	case MEAS_VOICE:
	case MEAS_CHAT:
		/* Только иконка (все подписи скрыты раскладкой) - счётчик сообщений
		   появится вместе с CONFIG_LORA. */
		break;

	default:   /* MEAS_DEFAULT / MEAS_NONE: тексты не трогаем */
		break;
	}
}

//===========================================================================================================================
// Экран измерений ЦЕЛИКОМ: страница state_menu -> режимы обеих панелей + данные.
// num = menu_status.state_menu (0..VECTOR_MEASURE_PAGES-1); выход за предел -
// первая страница (как current_page в LCD_SubMenuInfo).
// КОНТЕКСТ: поток LVGL Task, вызов из Menu_Draw() каждые 100 мс.
//===========================================================================================================================
void LCD_MeasureScreen(uint8_t num)
{
	static char value[2][MEAS_VALUE_SZ];
	static char name[2][MEAS_NAME_SZ];
	static char units[2][MEAS_UNITS_SZ];
	uint8_t page = (num >= (uint8_t)VECTOR_MEASURE_PAGES) ? 0u : num;
	uint8_t i;

	for (i = 0u; i < 2u; i++)
	{
		uint8_t mode = meas_pages[page][i];
		meas_fill(mode, value[i], name[i], units[i]);
		LCD_Measure(i, mode, value[i], name[i], units[i]);
	}
}

//===========================================================================================================================
/* Индикация батареи верхней панели (top layer: ui_BarBattery + ui_LabelPercent,
   Avis DisplayChargeIndication).
   precent        - заряд % из sensors_status.battery_charge_percent (MAX17048);
   state_flashing - фаза, инвертируемая раз в секунду (Menu_Draw,
                    TIMER_RTC_DISP_FLASHING_CHARGE): на каждой инверсии
                    «зарядка» добавляет сегмент +10% и гонит его до 100.
   Зарядка = ST_COMMON_BIT_BAT_CHARGE (ставит машина батареи Avis_Battery_System);
   после выдергивания провода анимация держится ещё LCD_BATTERY_CHARGE_HOLD_MS.
   ВРЕМЯ: тик RTOS проекта VTICK_MS() (vector_tick.h, ThreadX 10 мс) - портовские
   TickType_t/xTaskGetTickCount()/pdMS_TO_TICKS() из Avis здесь не существуют. */
void LCD_Battery(uint16_t precent, uint16_t state_flashing)
{
	uint16_t precent_disp = precent;
    static uint32_t charge_end_ms = 0;   // до какой метки (мс) держать «зарядку»
    uint8_t is_charging_active = 0;
    uint32_t current_ms = VTICK_MS();

    if (TEST_STATUS_COMMON_BIT(ST_COMMON_BIT_BAT_CHARGE)) {
        // Провод подключен: рассчитываем точку окончания (сейчас + задержка удержания)
        charge_end_ms = current_ms + LCD_BATTERY_CHARGE_HOLD_MS;
        is_charging_active = 1;
    }
    else {
        // Провода нет: проверяем, не ушло ли текущее время за точку окончания.
        // Знаковая разница беззнаковых меток корректна и при переполнении счётчика мс:
        if ((int32_t)(charge_end_ms - current_ms) > 0) {
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
	/* Текст % (str_lcd - uint8_t[], поэтому (char*)-каст; snprintf с размером -
	   без переполнения; лишний аргумент precent из форматной строки " " убран -
	   GCC ругался -Wformat-extra-args).                                      */
#if (CONFIG_BATTERY == 1)
	if(is_charging_active){
		snprintf((char*)str_lcd, sizeof(str_lcd), " ");
	}
	else{
		snprintf((char*)str_lcd, sizeof(str_lcd), "%d%%", (int)precent);
	}
#else
	snprintf((char*)str_lcd, sizeof(str_lcd), "%d%%", (int)precent);
#endif
	ui_set_label_text(ui_LabelPercent, (char*)str_lcd);
}

//===========================================================================================================================
void LCD_DataTime(struct tm*tm_ptr, uint8_t rank, uint8_t state_flashing)
{
	/* str_lcd объявлен как uint8_t[30], отсюда (char*)-каст (как в
	   LCD_Battery) - иначе -Wpointer-sign; snprintf с sizeof + аргументы в
	   фактических диапазонах полей tm закрывают -Wformat-overflow и
	   -Wformat-truncation. Реальный вывод "31.12.2026 23:59" = 16 + '\0';
	   позиции символов для b_idx (мигание разряда) при этом не меняются. */
	snprintf((char*)str_lcd, sizeof(str_lcd), "%.2d.%.2d.%.4d %.2d:%.2d",
			(uint8_t)tm_ptr->tm_mday,
			(uint8_t)(tm_ptr->tm_mon + 1),
			(uint16_t)(tm_ptr->tm_year + 1900),
			(uint8_t)tm_ptr->tm_hour,
			(uint8_t)tm_ptr->tm_min);
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
	ui_update_input_panel_head_enter((const char*)str_lcd, rank, b_idx, 12, 13, ROLLER_DATATIME);
}

//===========================================================================================================================
void LCD_Data(struct tm*tm_ptr, uint8_t rank, uint8_t state_flashing)
{
	/* (char*)-каст и snprintf - см. LCD_DataTime; поля tm в своих диапазонах. */
	snprintf((char*)str_lcd, sizeof(str_lcd), "%.2d.%.2d.%.4d",
			 (uint8_t)tm_ptr->tm_mday, (uint8_t)(tm_ptr->tm_mon + 1),
			 (uint16_t)(tm_ptr->tm_year + 1900));
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
    ui_update_input_panel_head_enter((const char*)str_lcd, rank, b_idx, 9, 8, ROLLER_DATA);
}

//===========================================================================================================================
void LCD_Time(struct tm*tm_ptr, uint8_t rank, uint8_t state_flashing)
{
	/* (char*)-каст и snprintf - см. LCD_DataTime; поля tm в своих диапазонах. */
	snprintf((char*)str_lcd, sizeof(str_lcd), "%.2d:%.2d",
			 (uint8_t)tm_ptr->tm_hour, (uint8_t)tm_ptr->tm_min);
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
    ui_update_input_panel_head_enter((const char*)str_lcd, rank, b_idx, 13, 12, ROLLER_TIME);
}

//===========================================================================================================================
// Отображение целого числа и мигание разрядом
void LCD_Digit_int_flashing_password(uint32_t data, uint8_t rank, uint8_t state_flashing)
{
    /* data - uint32_t: %u и (unsigned) (-Wformat), (char*)-каст потому что
       str_lcd это uint8_t[] (-Wpointer-sign). */
    snprintf((char*)str_lcd, sizeof(str_lcd), "%03u", (unsigned)data);
    //-------------------------------
	int8_t b_idx = (state_flashing && rank >= 2 && rank <= 4) ? (rank - 2) : -1;

	ui_update_input_panel_head_enter((const char*)str_lcd, rank, b_idx, 5, 6, ROLLER_PASSWORD);
}

//===========================================================================================================================
void LCD_TimeHead(struct tm*tm_ptr)
{
//	len_str = sprintf(str_lcd, "%.2d:%.2d", tm_ptr->tm_hour, tm_ptr->tm_min);
//	//-------------------------------
//	ui_set_label_text(ui_LabelTime, str_lcd);
//	memset(&str_lcd, 0, sizeof(str_lcd));
	//-------------------------------
}

//===========================================================================================================================
void LCD_CountdownSetTimer(uint32_t data, uint16_t roller)
{
	/* data - uint32_t: %u и (unsigned) (-Wformat); snprintf с sizeof и
	   (char*)-каст (-Wpointer-sign); snprintf возвращает int, len_str uint8_t. */
	len_str = (uint8_t)snprintf((char*)str_lcd, sizeof(str_lcd), "%u", (unsigned)data);
	//-------------------------------
	ui_set_label_text(ui_LabelSetEnter, _(text_labels[roller]));
	ui_set_label_text(ui_LabelSetEnter, (const char*)str_lcd);
	memset(str_lcd, 0, sizeof(str_lcd));
	//-------------------------------
	ui_next_state.head_timer = false;
	//-------------------------------
}

//===========================================================================================================================
void LCD_CountdownToSOS(uint32_t data, uint32_t state)
{
	LCD_CountdownSetTimer(data, ROLLER_SOS);
}

//===========================================================================================================================
void LCD_CountdownToShutdown(uint32_t data)
{
	LCD_CountdownSetTimer(data, ROLLER_TURN_OFF);
}

//===========================================================================================================================
void LCD_CountdownToWarm(uint32_t data)
{
//	LCD_CountdownSetTimer(data, ROLLER_WARM);
}

//===========================================================================================================================
void LCD_ToWarm(SNS_CFG *pSnsCfg, uint16_t num, uint8_t chan)
{
	switch(num)
	{
	case 0:
		SET_MIRAX(1);
		break;
	default:
	break;
	}
}

//===========================================================================================================================
void LCD_Firmware(void)
{
//	sprintf(str_lcd, _("Прошивка"));
//	ui_set_label_text(ui_LabelInfoStr, str_lcd);
//	ui_next_state.info_str = false;
}

#endif
