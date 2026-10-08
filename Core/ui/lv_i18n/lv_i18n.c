#include "./lv_i18n.h"


////////////////////////////////////////////////////////////////////////////////
// Define plural operands
// http://unicode.org/reports/tr35/tr35-numbers.html#Operands

// Integer version, simplified

#define UNUSED(x) (void)(x)

static inline uint32_t op_n(int32_t val) { return (uint32_t)(val < 0 ? -val : val); }
static inline uint32_t op_i(uint32_t val) { return val; }
// always zero, when decimal part not exists.
static inline uint32_t op_v(uint32_t val) { UNUSED(val); return 0;}
static inline uint32_t op_w(uint32_t val) { UNUSED(val); return 0; }
static inline uint32_t op_f(uint32_t val) { UNUSED(val); return 0; }
static inline uint32_t op_t(uint32_t val) { UNUSED(val); return 0; }

static lv_i18n_phrase_t en_gb_singulars[] = {
    {"SOS", "SOS"},
    {"Автокалибровка", "Auto calibration"},
    {"Английский", "English"},
    {"Ввод пароля", "Enter Password"},
    {"Версия FW", "FW Ver."},
    {"Версия HW", "HW Ver."},
    {"Время", "Time"},
    {"Время BUMP TEST", "BUMP TEST time"},
    {"Время калибровки", "Calib time"},
    {"Все каналы", "All channels"},
    {"Выбор языка", "Language Selection"},
    {"Выключение", "Shutdown"},
    {"Выполнено", "Done"},
    {"Газ", "Gas"},
    {"Группа", "Group"},
    {"Группа SOS", "Group SOS"},
    {"Далее", "Next"},
    {"Дата", "Date"},
    {"Дата и время", "Date & Time"},
    {"Дата, время", "Date & Time"},
    {"ДИ", "Range"},
    {"Дискр.", "Disc."},
    {"Ед. изм.", "Units"},
    {"Единицы измерения", "Units of Measurement"},
    {"Ждите", "Wait"},
    {"Зав.номер", "Ser. No."},
    {"Завершение калибровки датчика", "Completing sensor calibration"},
    {"Заводские настройки", "Factory settings"},
    {"Зона", "Zone"},
    {"Имя FW", "FW Name"},
    {"Информация", "Information"},
    {"Калиб. диапазона", "Span Calibration"},
    {"Калибровка диапазона", "Span Calibration"},
    {"Калибровка нуля", "Zero Calibration"},
    {"Калибровать", "Calibrate"},
    {"Концентр.", "Conc."},
    {"Крит. разряд батареи", "Crit. battery"},
    {"Назад", "Back"},
    {"Насос Вкл.", "Pump On"},
    {"Насос Выкл.", "Pump Off"},
    {"Настройка", "Settings"},
    {"Настройки", "Settings"},
    {"Настр. порог. значений", "Limit Settings"},
    {"Низкий заряд АКБ", "Low battery"},
    {"Общая", "General"},
    {"Один канал", "One channel"},
    {"Ок", "Ok"},
    {"Отмена", "Cancel"},
    {"Ош. калиб. диапаз.", "Span calib error"},
    {"Ош. калиб. нуля", "Zero calib error"},
    {"Ошибка", "Error"},
    {"Ошибка МЕМ", "MEM error"},
    {"Ошибка BLE", "BLE error"},
    {"Ошибка GPS", "GPS error"},
    {"Ошибка GSM", "GSM error"},
    {"Ошибка LCD", "LCD error"},
    {"Ошибка LORA", "LORA error"},
    {"Ошибка БАТ", "BAT error"},
    {"Ошибка ТЕМП", "TEMP error"},
    {"Ошибка сенсора", "Sensor failed"},
    {"Ошибки", "Errors"},
    {"Перезапуск сенсоров", "Restart Sensors"},
    {"Подайте нулевой газ", "Apply Zero Gas"},
    {"Подайте эталонный газ", "Apply Span Gas"},
    {"Подтвердить", "Confirm"},
    {"Порог 1", "Limit 1"},
    {"Порог 2", "Limit 2"},
    {"Порог 3", "Limit 3"},
    {"Прев. STEL", "STEL exceeded"},
    {"Прев. TWA", "TWA exceeded"},
    {"Прев. диапазона", "Range exceeded"},
    {"Прогрев", "Warm-up"},
    {"Русский", "Russian"},
    {"Сброс STEL, TWA, HIGH", "Reset STEL, TWA, HIGH"},
    {"Сброс ошибок", "Error reset "},
    {"Сер.номер", "Ser. No."},
    {"Сервисное меню", "Service Menu"},
    {"Систем. ош.", "Sys. Err."},
    {"Сохранить", "Save"},
    {"Тест", "Test"},
    {"Тест не пройден", "The test failed"},
    {"Тест пройден", "The test was passed"},
    {NULL, NULL} // End mark
};



static uint8_t en_gb_plural_fn(int32_t num)
{
    uint32_t n = op_n(num); UNUSED(n);
    uint32_t i = op_i(n); UNUSED(i);
    uint32_t v = op_v(n); UNUSED(v);

    if ((i == 1 && v == 0)) return LV_I18N_PLURAL_TYPE_ONE;
    return LV_I18N_PLURAL_TYPE_OTHER;
}

static const lv_i18n_lang_t en_gb_lang = {
    .locale_name = "en-GB",
    .singulars = en_gb_singulars,

    .locale_plural_fn = en_gb_plural_fn
};

static lv_i18n_phrase_t ru_ru_singulars[] = {
    {"SOS", "SOS"},
    {"Автокалибровка", "Автокалибровка"},
    {"Английский", "Английский"},
    {"Ввод пароля", "Введите пароль"},
    {"Версия FW", "Версия FW"},
    {"Версия HW", "Версия HW"},
    {"Время", "Время"},
    {"Время BUMP TEST", "Время BUMP TEST"},
    {"Время калибровки", "Время калибровки"},
    {"Все каналы", "Все каналы"},
    {"Выбор языка", "Выбор языка"},
    {"Выключение", "Выключение"},
    {"Выполнено", "Выполнено"},
    {"Газ", "Газ"},
    {"Группа", "Группа"},
    {"Группа SOS", "Группа SOS"},
    {"Далее", "Далее"},
    {"Дата", "Дата"},
    {"Дата и время", "Дата и время"},
    {"Дата, время", "Дата, время"},
    {"ДИ", "ДИ"},
    {"Дискр.", "Дискр."},
    {"Ед. изм.", "Ед. изм."},
    {"Единицы измерения", "Единицы измерения"},
    {"Ждите", "Ждите"},
    {"Зав.номер", "Зав.номер"},
    {"Завершение калибровки датчика", "Завершение калибровки датчика"},
    {"Заводские настройки", "Заводские настройки"},
    {"Зона", "Зона"},
    {"Имя FW", "Имя FW"},
    {"Информация", "Информация"},
    {"Калиб. диапазона", "Калиб. диапазона"},
    {"Калибровка диапазона", "Калибровка диапазона"},
    {"Калибровка нуля", "Калибровка нуля"},
    {"Калибровать", "Калибровать"},
    {"Концентр.", "Концентр."},
    {"Крит. разряд батареи", "Крит. разряд батареи"},
    {"Назад", "Назад"},
    {"Насос Вкл.", "Насос Вкл."},
    {"Насос Выкл.", "Насос Выкл."},
    {"Настройка", "Настройка"},
    {"Настройки", "Настройки"},
    {"Настр. порог. значений", "Настр. порог. значений"},
    {"Низкий заряд АКБ", "Низкий заряд АКБ"},
    {"Общая", "Общая"},
    {"Один канал", "Один канал"},
    {"Ок", "Ок"},
    {"Отмена", "Отмена"},
    {"Ош. калиб. диапаз.", "Ош. калиб. диапаз."},
    {"Ош. калиб. нуля", "Ош. калиб. нуля"},
    {"Ошибка", "Ошибка"},
    {"Ошибка МЕМ", "Ошибка МЕМ"},
    {"Ошибка BLE", "Ошибка BLE"},
    {"Ошибка GPS", "Ошибка GPS"},
    {"Ошибка GSM", "Ошибка GSM"},
    {"Ошибка LCD", "Ошибка LCD"},
    {"Ошибка LORA", "Ошибка LORA"},
    {"Ошибка БАТ", "Ошибка БАТ"},
    {"Ошибка ТЕМП", "Ошибка ТЕМП"},
    {"Ошибка сенсора", "Ошибка сенсора"},
    {"Ошибки", "Ошибки"},
    {"Перезапуск сенсоров", "Перезапуск сенсоров"},
    {"Подайте нулевой газ", "Подайте нулевой газ"},
    {"Подайте эталонный газ", "Подайте эталонный газ"},
    {"Подтвердить", "Подтвердить"},
    {"Порог 1", "Порог 1"},
    {"Порог 2", "Порог 2"},
    {"Порог 3", "Порог 3"},
    {"Прев. STEL", "Прев. STEL"},
    {"Прев. TWA", "Прев. TWA"},
    {"Прев. диапазона", "Прев. диапазона"},
    {"Прогрев", "Прогрев"},
    {"Русский", "Русский"},
    {"Сброс STEL, TWA, HIGH", "Сброс STEL, TWA, HIGH"},
    {"Сброс ошибок", "Сброс ошибок"},
    {"Сер.номер", "Сер.номер"},
    {"Сервисное меню", "Сервисное меню"},
    {"Систем. ош.", "Систем. ош."},
    {"Сохранить", "Сохранить"},
    {"Тест", "Тест"},
    {"Тест не пройден", "Тест не пройден"},
    {"Тест пройден", "Тест пройден"},
    {NULL, NULL} // End mark
};



static uint8_t ru_ru_plural_fn(int32_t num)
{
    uint32_t n = op_n(num); UNUSED(n);
    uint32_t v = op_v(n); UNUSED(v);
    uint32_t i = op_i(n); UNUSED(i);
    uint32_t i10 = i % 10;
    uint32_t i100 = i % 100;
    if ((v == 0 && i10 == 1 && i100 != 11)) return LV_I18N_PLURAL_TYPE_ONE;
    if ((v == 0 && (2 <= i10 && i10 <= 4) && (!(12 <= i100 && i100 <= 14)))) return LV_I18N_PLURAL_TYPE_FEW;
    if ((v == 0 && i10 == 0) || (v == 0 && (5 <= i10 && i10 <= 9)) || (v == 0 && (11 <= i100 && i100 <= 14))) return LV_I18N_PLURAL_TYPE_MANY;
    return LV_I18N_PLURAL_TYPE_OTHER;
}

static const lv_i18n_lang_t ru_ru_lang = {
    .locale_name = "ru-RU",
    .singulars = ru_ru_singulars,

    .locale_plural_fn = ru_ru_plural_fn
};

const lv_i18n_language_pack_t lv_i18n_language_pack[] = {
    &en_gb_lang,
    &ru_ru_lang,
    NULL // End mark
};

////////////////////////////////////////////////////////////////////////////////


// Internal state
static const lv_i18n_language_pack_t * current_lang_pack;
static const lv_i18n_lang_t * current_lang;


/**
 * Reset internal state. For testing.
 */
void __lv_i18n_reset(void)
{
    current_lang_pack = NULL;
    current_lang = NULL;
}

/**
 * Set the languages for internationalization
 * @param langs pointer to the array of languages. (Last element has to be `NULL`)
 */
int lv_i18n_init(const lv_i18n_language_pack_t * langs)
{
    if(langs == NULL) return -1;
    if(langs[0] == NULL) return -1;

    current_lang_pack = langs;
    current_lang = langs[0];     /*Automatically select the first language*/
    return 0;
}

/**
 * Change the localization (language)
 * @param l_name name of the translation locale to use. E.g. "en-GB"
 */
int lv_i18n_set_locale(const char * l_name)
{
    if(current_lang_pack == NULL) return -1;

    uint16_t i;

    for(i = 0; current_lang_pack[i] != NULL; i++) {
        // Found -> finish
        if(strcmp(current_lang_pack[i]->locale_name, l_name) == 0) {
            current_lang = current_lang_pack[i];
            return 0;
        }
    }

    return -1;
}


static const char * __lv_i18n_get_text_core(lv_i18n_phrase_t * trans, const char * msg_id)
{
    uint16_t i;
    for(i = 0; trans[i].msg_id != NULL; i++) {
        if(strcmp(trans[i].msg_id, msg_id) == 0) {
            /*The msg_id has found. Check the translation*/
            if(trans[i].translation) return trans[i].translation;
        }
    }

    return NULL;
}


/**
 * Get the translation from a message ID
 * @param msg_id message ID
 * @return the translation of `msg_id` on the set local
 */
const char * lv_i18n_get_text(const char * msg_id)
{
    if(current_lang == NULL) return msg_id;

    const lv_i18n_lang_t * lang = current_lang;
    const void * txt;

    // Search in current locale
    if(lang->singulars != NULL) {
        txt = __lv_i18n_get_text_core(lang->singulars, msg_id);
        if (txt != NULL) return txt;
    }

    // Try to fallback
    if(lang == current_lang_pack[0]) return msg_id;
    lang = current_lang_pack[0];

    // Repeat search for default locale
    if(lang->singulars != NULL) {
        txt = __lv_i18n_get_text_core(lang->singulars, msg_id);
        if (txt != NULL) return txt;
    }

    return msg_id;
}

/**
 * Get the translation from a message ID and apply the language's plural rule to get correct form
 * @param msg_id message ID
 * @param num an integer to select the correct plural form
 * @return the translation of `msg_id` on the set local
 */
const char * lv_i18n_get_text_plural(const char * msg_id, int32_t num)
{
    if(current_lang == NULL) return msg_id;

    const lv_i18n_lang_t * lang = current_lang;
    const void * txt;
    lv_i18n_plural_type_t ptype;

    // Search in current locale
    if(lang->locale_plural_fn != NULL) {
        ptype = lang->locale_plural_fn(num);

        if(lang->plurals[ptype] != NULL) {
            txt = __lv_i18n_get_text_core(lang->plurals[ptype], msg_id);
            if (txt != NULL) return txt;
        }
    }

    // Try to fallback
    if(lang == current_lang_pack[0]) return msg_id;
    lang = current_lang_pack[0];

    // Repeat search for default locale
    if(lang->locale_plural_fn != NULL) {
        ptype = lang->locale_plural_fn(num);

        if(lang->plurals[ptype] != NULL) {
            txt = __lv_i18n_get_text_core(lang->plurals[ptype], msg_id);
            if (txt != NULL) return txt;
        }
    }

    return msg_id;
}

/**
 * Get the name of the currently used locale.
 * @return name of the currently used locale. E.g. "en-GB"
 */
const char * lv_i18n_get_current_locale(void)
{
    if(!current_lang) return NULL;
    return current_lang->locale_name;
}
