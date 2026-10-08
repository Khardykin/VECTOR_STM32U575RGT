# LVGL 9.2 + SquareLine Studio: как устроено, где лежит, как собирать

Коротко: графика в проекте — **LVGL версии 9.2** (репозиторий
<https://github.com/lvgl/lvgl.git>), экранные формы рисуются в **SquareLine
Studio** и экспортируются в проект как обычные C-файлы (`ui.c/ui.h`, screens,
components, images, fonts). Ниже — что где лежит, чего в git нет, какие пути
прописаны в проекте, и готовый шаблон интеграции с ThreadX.

---

## 1. Что где лежит

| Что | Где | В git? |
|---|---|---|
| LVGL 9.2 (библиотека) | `Drivers/lvgl` | **нет** (`/Drivers/` в `.gitignore`) |
| UI от SquareLine Studio (`ui.h`, `ui.c`, screens/components, `images/`, `fonts/`) | `Drivers` (экспорт из Studio) | **нет** |
| `lv_conf.h` (конфигурация LVGL) | должен лежать в include path (рекомендуется `Core/Inc/lv_conf.h`) | **нет** |
| Переводы (исходники) | `Core/translations/ru-RU.yml`, `en-GB.yml` | да |
| Сгенерированный i18n | `Core/ui/lv_i18n/lv_i18n.c/h` | да |
| Скрипт перегенерации переводов | `Core/update_lang.bat` | да |
| Драйвер панели (HAL, SPI2+DMA) | `Core/VectorLib/Src/TFT/LCD_platform.c/h` | да |
| Инициализация контроллера ST7789P3 | `Core/VectorLib/Src/TFT/TFT.c/h` | да |
| Индикация прибора (меню/экраны Avis) | `Core/VectorLib/Src/TFT/TFT_indicator.c/h` | да, **выключена** (`CONFIG_TYPE_LCD == 2`, ждёт переноса) |

Что уже настроено в проекте (`.cproject`, обе конфигурации Debug и Release):

* `Drivers` — source path с исключением `lvgl/tests|lvgl/examples|lvgl/demos`
  (иначе LVGL тянет в сборку свои тесты и примеры);
* include paths: `../Drivers/lvgl`, `../Drivers/ui`, `../Core/ui`,
  `../Core/ui/lv_i18n`, `../Core/VectorLib/Src/TFT`, `../Core/VectorLib/Src/BME280`,
  `../Core/VectorLib/Src/Lis3dh_reg`.

> **Про `.gitignore`**: строка `!/Debug/ui_Vector/`, стоящая ПОСЛЕ `/Debug/`,
> не работает — git не заходит внутрь исключённого каталога. Рабочий вариант,
> если UI когда-нибудь понадобится в git: `/Debug/*` и затем
> `!/Debug/ui_Vector/`. Сейчас UI лежит в `Drivers/`, так что строка просто
> не используется.
>
> **Следствие**: собрать прошивку «из git» без LVGL и UI нельзя. Для
> воспроизводимой сборки держите версии в одном месте: тег LVGL
> (`v9.2.x`), файл проекта SquareLine Studio и `lv_conf.h` — в репозитории или
> в общем хранилище, а не только на рабочей машине.

---

## 2. Настройки проекта SquareLine Studio

| Параметр | Значение | Почему |
|---|---|---|
| LVGL version | **9.2** | совпадает с `Drivers/lvgl` |
| Display resolution | **172 × 320** | панель ST7789P3 (`TFT_WIDTH`/`TFT_HEIGHT` в `TFT.h`) |
| Color depth | **16 bit (RGB565)** | `TFT_WriteByte(0x55)` = COLMOD RGB565 в `TFT_Init()` |
| Rotation | 0 (портрет) | поворотом управляет акселерометр: `TFT_Rotation(0/2)` |
| LVGL include path | `Drivers/lvgl` | — |
| Export UI Files | в каталог из include path (например `Drivers/ui`) | чтобы `#include "ui.h"` находил его без правок |

После экспорта в проект попадают `ui.h/ui.c`, `ui_helpers.*`, `ui_events.*`,
`screens/`, `components/`, `images/`, `fonts/` — их компилирует CubeIDE как
обычные исходники (каталог должен быть внутри source path, `Drivers` подходит).

---

## 3. `lv_conf.h`: минимум для этой платы

```c
#define LV_COLOR_DEPTH     16          /* RGB565, как в TFT_Init (COLMOD 0x55) */

/* Память LVGL: встроенный аллокатор и его размер (под 172x320 и пару экранов) */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_MEM_SIZE             (96U * 1024U)

/* Тик: в проекте время — тик RTOS (vector_tick.h) либо TIM3 1 кГц.
   Вариант А (проще, разрешение 10 мс): */
#define LV_TICK_CUSTOM          1
#define LV_TICK_CUSTOM_INCLUDE  "vector_tick.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (VTICK_MS())
/* Вариант Б (плавные анимации): lv_tick_inc(1) из TIM3 1 кГц в
   HAL_TIM_PeriodElapsedCallback, LV_TICK_CUSTOM не включать.                 */

#define LV_USE_LOG              0      /* свой лог: vector_log.c */
#define LV_FONT_MONTSERRAT_14   1      /* и те, что использует UI */
```

Если Studio экспортирует UI со своими шрифтами — включите в `lv_conf.h` именно
их, иначе линкер не найдёт `lv_font_montserrat_*`.

---

## 4. Интеграция с ThreadX: поток `LVGL Task`

Поток уже создан в `AZURE_RTOS/App/app_azure_rtos.c` (`lvgl_thread_entry`,
приоритет 15, стек `LVGL_STACK_SIZE` = 4096) и пока просто спит. Шаблон
наполнения — вставить в `USER CODE` этого файла (или в свой `lv_port.c`):

```c
#include "lvgl.h"
#include "ui.h"                 /* SquareLine Studio */
#include "lv_i18n.h"            /* Core/ui/lv_i18n */
#include "TFT.h"                /* TFT_Init / TFT_FlushBuffer / TFT_Rotation */
#include "LCD_platform.h"
#include "vector_tick.h"

/* Буферы partial rendering: 1/10 экрана = 172*32*2 = 11 КБ на буфер.
   TFT_indicator.h уже задаёт BUF_SIZE = TFT_WIDTH*TFT_HEIGHT/4 (27 КБ).      */
static lv_color_t fb1[TFT_WIDTH * TFT_HEIGHT / 10];
static lv_color_t fb2[TFT_WIDTH * TFT_HEIGHT / 10];

lv_display_t *disp;             /* его ждёт LCD_platform.c (weak-символ там) */

static void disp_flush(lv_display_t *d, const lv_area_t *area, uint8_t *px_map)
{
  /* TFT_FlushBuffer -> LCD_writeBulk -> SPI2 DMA -> LCD_transferCpltCallback
     -> lv_display_flush_ready(disp). То есть вызов НЕ блокирующий: готовность
     сообщает прерывание DMA.                                                 */
  TFT_FlushBuffer(area->x1, area->y1,
                  (uint16_t)lv_area_get_width(area),
                  (uint16_t)lv_area_get_height(area), px_map);
}

void lv_port_init(void)
{
  lv_init();
  lv_tick_set_cb(VTICK_MS);     /* или lv_tick_inc(1) из TIM3, см. lv_conf.h */

  TFT_Init();                   /* ST7789P3: SPI2, 172x320, backlight_set(40) */

  disp = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
  lv_display_set_buffers(disp, fb1, fb2, sizeof(fb1), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, disp_flush);

  lv_i18n_init(lv_i18n_language_pack);
  lv_i18n_set_locale("ru-RU");

  ui_init();                    /* экраны SquareLine Studio */
}
```

и тело потока:

```c
void lvgl_thread_entry(ULONG thread_input)
{
  lv_port_init();
  while (1)
  {
    lv_timer_handler();
    VTICK_SLEEP_MS(VECTOR_LVGL_PERIOD_MS);   /* 5..33 мс; меньше тика (10 мс)
                                                RTOS не даёт, поэтому 10 */
  }
}
```

Три момента, на которых обычно спотыкаются:

1. **Стек потока.** 4096 байт для LVGL 9 мало: поднимите `LVGL_STACK_SIZE` до
   8192–16384 (`app_azure_rtos.c`, `USER CODE PD`).
2. **Кто владеет экраном.** `TFT_Rotation()` из `Vector_Run_Measure()` (Measure
   Task) и `lv_timer_handler()` (LVGL Task) трогают один SPI2. Поворот экрана
   безопасен, потому что `LCD_platform.c` сериализует доступ флагом
   `LCD_SPI_PORT_State` с ожиданием и таймаутом (`VECTOR_LCD_SPI_TIMEOUT_MS`),
   но рисование LVGL на время поворота лучше приостанавливать
   (`lv_display_set_rotation()` вместо ручного `TFT_Rotation()`, если экран
   переворачивается средствами LVGL).
3. **Приоритеты.** Audio Player = 10, Receiver = 12, Measure = 13, LVGL = 15.
   Графика не должна обгонять звук и обмен — так и есть.

---

## 5. Порядок байт RGB565 (цвета «не те»)

LVGL рисует пиксель в порядке байт хоста (little-endian: младший первым), а
ST7789P3 принимает **старший байт пикселя первым**. Поэтому в
`LCD_platform.c` обмен байтов делает `VECTOR_LCD_SWAP_RGB565` (по умолчанию 1)
— внутри `LCD_writeBulk()`/`LCD_writeData16Bit()`, до старта DMA.

Альтернатива (если хочется менять байты в своём flush-callback): в LVGL 9 есть
`lv_draw_sw_rgb565_swap(px_map, len)` — тогда поставьте `VECTOR_LCD_SWAP_RGB565 0`
в `vector_config.h`, иначе байты поменяются дважды и цвета будут inverted.

---

## 6. Переводы (i18n): `Core/translations` + `lv_i18n`

Схема: ключ перевода — русский текст из `ru-RU.yml`, английский берётся из
`en-GB.yml`.

1. Правите/добавляете строки в `Core/translations/ru-RU.yml` и `en-GB.yml`
   (ключи обязаны совпадать).
2. Из каталога `Core/` запускаете `update_lang.bat`:
   ```
   lv_i18n compile -t "translations/*.yml" -o "ui/lv_i18n"
   ```
   (нужен CLI: `npm install -g lv_i18n`). Скрипт перезаписывает
   `Core/ui/lv_i18n/lv_i18n.c/h` — они в git, так что результат виден в diff.
3. В коде UI: `lv_label_set_text(label, _("Время"));`, для форм с числом —
   `_p("канал", n)`. В SquareLine Studio в поле текста пишете тот же ключ
   (`Время`), а `_("...")` применяете в `ui_events.c` или оборачиваете
   сгенерированный текст при экспорте.
4. Инициализация и смена языка: `lv_i18n_init(lv_i18n_language_pack);`
   `lv_i18n_set_locale("ru-RU");`. При смене языка в рантайме тексты уже
   созданных объектов не обновляются — экраны пересоздают (`ui_init()` заново
   или `lv_screen_load_anim()` с пересборкой нужного экрана).

Языков сейчас два: `ru-RU` и `en-GB` (`lv_i18n_language_pack` в
`Core/ui/lv_i18n/lv_i18n.c`).

---

## 7. Панель и SPI2: что проверить в CubeMX

| Параметр | Где | Значение |
|---|---|---|
| SPI2 | Connectivity → SPI2 | Mode **Simplex Bidirectional Master** (только TX), **8 бит**, MSB first, CPOL Low / CPHA 1 Edge |
| SPI2 prescaler | там же | **/4 (40 МГц) или /8 (20 МГц)**. Сейчас стоит /2 = 80 МГц — это выше допустимого для ST7789P3 (запись ~62.5 МГц max): возможны битые пиксели и зависания выдачи кадра |
| SPI2 DMA | GPDMA1 Channel8 | Request **SPI2_TX**, Memory→Periph, **BYTE/BYTE**, SrcInc, **Normal** mode |
| NVIC | GPDMA1_Channel8_IRQn, SPI2_IRQn | включены (нужны для `HAL_SPI_TxCpltCallback` → `lv_display_flush_ready`) |
| Пиновое управление | PB14 LCD_CS, PB10 LCD_DC, PB12 LCD_RST, PC6 LCD_LED | GPIO Output |

Смещение окна панели: у ST7789P3 RAM 240×320, а панель 172×320, поэтому в
`TFT.h` заданы `TFT_COL_OFFSET` (34) и `TFT_ROW_OFFSET` (0) — они прибавляются
в `TFT_SetAddressWindow()`. Если картинка сдвинута или обрезана по
горизонтали — поправьте `TFT_COL_OFFSET` (типовые значения 34, 0 или 68 в
зависимости от ревизии панели).

---

## 8. Чек-лист «чего не хватает до работающей графики»

* [ ] `lv_conf.h` в include path (раздел 3) и в git
* [ ] LVGL 9.2 в `Drivers/lvgl` (тег зафиксировать в README/PLAN)
* [ ] UI, экспортированный из SquareLine Studio, в include path (`../Drivers/ui`)
* [ ] порт дисплея и тело потока `LVGL Task` (раздел 4) + `LVGL_STACK_SIZE` 8–16 КБ
* [ ] тик LVGL: `lv_tick_set_cb(VTICK_MS)` или `lv_tick_inc(1)` из TIM3
* [ ] `lv_i18n_init()` + `lv_i18n_set_locale()` до `ui_init()`
* [ ] SPI2 prescaler /4 или /8 в кубе (раздел 7)
* [ ] `TFT_indicator.c` — перенос слоя индикации Avis (нужны `SNS_CFG_Type`,
      `CALIB_CFG`, `COUNT_CHAN`, `DEVICE_NUMBER Device2/3_Pro`); пока файл
      выключен (`CONFIG_TYPE_LCD == 2`), экран можно гонять через LVGL или
      `TFT_Test()`
