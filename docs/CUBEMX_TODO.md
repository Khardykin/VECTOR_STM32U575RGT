# Что перенести в CubeMX (и что после этого удалить из кода)

Правило проекта: **конфигурацию периферии делает CubeMX**, в сгенерированных
файлах остаются только вызовы наших модулей внутри `USER CODE`. Ниже — что
сейчас дописано руками, где это настраивается в кубе и что удалить после
перегенерации.

Проверено по состоянию репозитория: руки в сгенерированных файлах торчат только
в трёх местах — `spi.c` (`USER CODE SPI1_Init 2`, 23 строки), `main.c`
(`USER CODE 2`, 49 строк) и `main.c` (`Callback 1`, одна строка вызова — её
оставляем). Остальные `Core/Src/*.c` чистые.

---

## 1. SPI1: 8 бит, 20 МГц, быстрые пины — `spi.c`, `USER CODE SPI1_Init 2`

Сейчас куб генерирует SPI1 с 4-битными фреймами и делителем /2 (80 МГц) —
внешняя flash так не разговаривает, поэтому блок переинициализирует SPI1 и
пины руками.

**В CubeMX** (Connectivity → SPI1 → Mode: Full-Duplex Master → Parameter
Settings):

| Параметр | Значение |
|---|---|
| Frame Format | Motorola |
| Data Size | **8 Bits** |
| First Bit | MSB First |
| Clock Polarity (CPOL) | Low |
| Clock Phase (CPHA) | 1 Edge |
| NSS Signal | Software Management |
| Baud Rate Prescaler | **8** → 160 МГц / 8 = **20 МГц** |
| Master SS Idleness | 00 cycle |
| Master Inter-Data Idleness | 00 cycle |

**Пины** (System Core → GPIO или клик по PA5/PA6/PA7):

| Параметр | Значение |
|---|---|
| GPIO mode | Alternate Function Push Pull |
| GPIO Pull-up/Pull-down | No pull-up and no pull-down |
| **Maximum output speed** | **High** (сейчас Low — на 20 МГц фронты пологие, биты плывут) |
| User Label | SCK / MISO / MOSI |

**CS (PA4)**: System Core → GPIO → PA4 → GPIO_Output → *GPIO output level =
**High*** (чтобы до первого обращения чип не был выбран).

После этого из `spi.c` можно удалить весь блок `USER CODE BEGIN SPI1_Init 2`.

---

## 2. PC9 (SD_MODE) = High при старте — `main.c`, `USER CODE 2`

Усилитель сидит в shutdown, пока SD_MODE низкий: звука не будет вообще, даже
при полностью рабочем SAI/DMA. Сейчас пин поднимается руками + грубая задержка
циклом на ~5 мс.

**В CubeMX**: System Core → GPIO → PC9 → GPIO_Output →
**GPIO output level = High**, Maximum output speed = Low.

После этого из `main.c` удаляются `HAL_GPIO_WritePin(SD_MODE...)` и блок
задержки: `MX_GPIO_Init()` вызывается в самом начале `main()`, к моменту
первого звука пройдёт не один миллисекунд.

---

## 3. Приоритет тайм-базы TIM6: 15 → 5

TIM6 (тайм-база HAL) имеет **самый низкий** приоритет в проекте, поэтому его
прерывание теряется на фоне EXTI/SPI/GPDMA/UART (приоритеты 0–1) и остановок
ядра отладчиком. В логе от 11:19 за 10 реальных секунд пришло 148 прерываний
вместо 10000.

**В CubeMX**: вкладка **NVIC** → `TIM6 global interrupt` → Preemption Priority
**15 → 5** (Sub Priority 0). Для ThreadX это безопасно: порт Cortex-M33
маскирует критические секции через PRIMASK, а не по порогу приоритета, поэтому
вызовы `tx_*` из ISR допустимы при любом приоритете.

Основной код на HAL-тик больше не опирается (всё время — от тика RTOS,
`vector_tick.h`), но таймауты внутри HAL-драйверов (`HAL_SPI_Transmit(...,
250)`) считаются по `HAL_GetTick()`, так что ровный тик всё равно полезен.

---

## 4. UART4: однопроводный полудуплекс → обычный асинхронный

Сейчас в `.ioc`: `PC10.Mode = Half_duplex(single_wire_mode)`, приёмника нет
(PC11 был занят `ACCEL_INT`, теперь он свободен — `GPIO_MODE_ANALOG`).
Отсюда три проблемы: приёмник слышит собственную передачу (эхо), линия
открытый сток без подтяжки, обычный 2-проводный терминал не подключить.

**В CubeMX**: Connectivity → **UART4** → Mode: **Asynchronous**;
пины PC10 = UART4_TX, PC11 = UART4_RX (если куб предложит другие — поменяйте
в Pinout). В GPIO для обоих: Alternate Function Push Pull, speed Low, **No
pull-up/pull-down** (или Pull-up, если линия длинная).

Результат: `HAL_UART_Init` вместо `HAL_HalfDuplex_Init`, push-pull выходы,
эха нет. Код моста подстроится сам: `ub_transmit()` проверяет бит `HDSEL` в
рантайме и глушит свой приём только в полудуплексе.

После этого лог и мост смогут жить на UART4 одновременно без оговорок.

---

## 5. PB3: BUTTON3 или SWO-консоль — что-то одно

PB3 = **JTDO/TRACESWO**. Пока на нём кнопка, вывод ITM/SWO физически
невозможен, то есть `VECTOR_LOG_ITM 1` ничего не печатает в консоль CubeIDE.

* Хотите лог **в консоли CubeIDE** без проводов: уберите BUTTON3 с PB3 (или
  вообще снимите с него EXTI), оставьте PB3 как `SYS_JTDO-SWV`, и в
  Run Configurations → Debugger включите **Serial Wire Viewer (SWV)** с
  Core Clock = 160 MHz.
* Хотите оставить кнопку: используйте `VECTOR_LOG_UART 4` (терминал) и
  не включайте SWV.

---

## 6. EXTI8 (CHARGE_STATE, PC8) настроен, но прерывание не включено

В `gpio.c` PC8 сконфигурирован как `GPIO_MODE_IT_RISING_FALLING` + `NOPULL`,
но в NVIC `EXTI8_IRQn` не включён — события от зарядного устройства никуда не
приходят.

**В CubeMX**: вкладка NVIC → `EXTI line[8] interrupt` → **Enabled**,
Preemption Priority 5–8. И заодно решите подтяжку: `NOPULL` на входе, к
которому ничего не подключено, даёт плавающий уровень и ложные срабатывания —
поставьте Pull-up/Pull-down по схеме.

---

## 7. RTC WakeUp: счётчик в секундах

В `.ioc` включён `RTC_WAKEUPCLOCK_CK_SPRE_17BITS`. CK_SPRE = **1 Гц**, поэтому
период = (WUT + 1) **секунд** — это источник «таймера на 1 секунду», который
легко спутать с системным тиком. Если нужен более мелкий шаг —
`RTC_WAKEUPCLOCK_RTCCLK_DIV16` (LSI 32 кГц / 16 ≈ 2 кГц, шаг ~0.5 мс).

---

## Что должно остаться в `USER CODE` (это не конфигурация периферии)

`main.c`, `USER CODE BEGIN 2`:

```c
  vector_sys_init();     /* приборы времени + заморозка тайм-базы под отладчиком */
  (void)sf_probe();      /* проба внешней flash */
  LOG_I(VLOG_M_SYS, "probe rc=%d jedec=%x %x %x", ...);
  audio_selftest();      /* N писков напрямую, без RTOS */
```

`main.c`, `USER CODE BEGIN Callback 1`:

```c
  vector_sys_tick_hook(htim);
```

`AZURE_RTOS/App/app_azure_rtos.c`, `tx_application_define`:

```c
  vlog_init();  ext_init();  audio_init();  uart_bridge_init();
```

и в потоке LVGL: `vector_sys_tick_report();`

Всё остальное (пины, тактирование, DMA, NVIC, режимы UART/SPI/SAI) — из куба.

---

## Проверено и трогать не нужно

* SAI1_Block_A: I2S standard, 16 бит, 2 слота, 8 кГц, MCK выключен, PLL3
  4.096 МГц — `HAL_SAI_InitProtocol` считает делитель сам;
* GPDMA1: ch9 = SPI1_TX, ch10 = SPI1_RX, ch11 = SAI1_A, все три IRQ включены;
* SPI1_IRQn включён — он **обязателен** для DMA-чтения: HAL вызывает
  `HAL_SPI_RxCpltCallback` из прерывания EOT самого SPI, а не из DMA;
* тайм-база HAL = TIM6, `NVIC.TimeBase = TIM6_IRQn`;
* тик ThreadX = SysTick 100 Гц, настраивается в `tx_initialize_low_level.S`
  (`SYSTEM_CLOCK = 160000000`) — **если поменяете частоту ядра, правьте эту
  константу**, иначе `tx_thread_sleep()` поедет.
