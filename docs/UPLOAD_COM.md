# Загрузка во внешнюю flash по COM (USART3)

Без программатора: sounds.bin и любые другие данные льются по UART.
Протокол - как Avis Command.c (0x0F, адрес 32 бита), CRC16 Modbus.

    запрос: [slave][cmd][addr32 BE][size16 BE][payload][CRC16 lo,hi]
    ответ : тот же формат
    ошибка: [slave][cmd|0x80][code][CRC16 lo,hi]

| cmd | назначение | payload |
|---|---|---|
| 0x0A COMMIT | плеер перечитывает образ | - |
| 0x0B INFO | геометрия + заголовок | - / 23 Б |
| 0x0D ERASE | стереть (сектора 4 КБ) | len32 BE + key8 |
| 0x0E READ | прочитать (<=256 Б) | len16 BE |
| 0x0F WRITE | записать (<=1024 Б) + сверка | данные |

Коды: 1 длина, 2 адрес, 3 flash, 4 сверка, 5 защищено, 6 команда.
Стирание выше 0x400000 (CONFIG/LOG) - только с ключом 0xA5 (--force).

## Тракт

USART3_IRQHandler -> InputBuffer[TYPE_USART] (кольцо 254 Б) ->
command_message() в receiver_task каждые 10 мс -> Command_vector ->
sf_sector_erase/sf_program/sf_verify -> transmit_buffer(TYPE_USART).

command_message() в Command_vector.c - СИЛЬНОЕ определение, замещает
weak-заглушку main_vector.c; править main_vector.c и it.c не нужно.
Отключить привязку к COM: собрать с -DCMD_VECTOR_COM=0.

Конец кадра - по длине из шапки (не по паузе): сервис раз в 10 мс.
Сверка читает по 256 Б: ответ 1034 Б не влезает в таймаут TX 100 мс.
Стирание по 16 КБ: receiver_task не должен стоять полсекунды.

## Запуск

    py -m pip install pyserial
    py tools/upload_flash.py -p COM7 tools/sounds.bin
    py tools/upload_flash.py -p COM7 --info
    py tools/upload_flash.py -p COM7 --addr 0x400000 cfg.bin --force
    py tools/upload_flash.py -p COM7 --read 256 --addr 0 --out head.bin

115200 8N1, чанк 1024 Б -> ~11 КБ/с, 1 МБ примерно 1.5-2 мин.
Если сборка не видит spiflash.h/audio_player.h - добавь в include path
Core/VectorLib/Src/Audio/Inc.
