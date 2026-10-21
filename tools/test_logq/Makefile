# Хост-тест журнала v2 (геометрия кольца 40 Б, RAM-очередь, писатель).
# Заголовки - НАСТОЯЩИЕ (Core/VectorLib): тест и прошивка исполняют один код.
#   make run    - собрать и прогнать
#   make clean
CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O1
INC      = -I../../Core/VectorLib/Inc -I../../Core/VectorLib/Src/Audio/Inc

test_logq: test_logq.c
	$(CC) $(CFLAGS) $(INC) -o $@ $<

run: test_logq
	./test_logq

clean:
	rm -f test_logq
