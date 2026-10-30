# Хост-тест журнала v2 (геометрия кольца 40 Б, RAM-очередь, писатель).
# Заголовки - НАСТОЯЩИЕ (Core/VectorLib): тест и прошивка исполняют один код.
# log_queue.c - определения геометрии кольца и RAM-очереди (раньше они были
# static inline в Config_save_read.h; замечание автора «не надо держать функции
# в .h»). Файл чистый (только Config_save_read.h, без HAL/ThreadX), поэтому
# тест собирает его как есть - тот же код, что и в прошивке.
#   make run    - собрать и прогнать
#   make clean
CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O1
INC      = -I../../Core/VectorLib/Inc -I../../Core/VectorLib/Src/Audio/Inc
SRC      = ../../Core/VectorLib/Src/log_queue.c

test_logq: test_logq.c $(SRC)
	$(CC) $(CFLAGS) $(INC) -o $@ test_logq.c $(SRC)

run: test_logq
	./test_logq

clean:
	rm -f test_logq
