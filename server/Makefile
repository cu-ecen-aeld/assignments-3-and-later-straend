.PHONY: all default clean valgrind

SRC := aesdsocket.c
TARGET = aesdsocket
CC = gcc
CFLAGS += -g -lpthread -Wall -Werror

all: default

default: aesdsocket

aesdsocket: aesdsocket.c
	$(CROSS_COMPILE)$(CC) -o $(TARGET) $(SRC)

clean:
	-rm -f $(TARGET)

valgrind: aesdsocket
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=/tmp/valgrind-aesdsocket.txt ./aesdsocket
#	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=/home/ubuntu/coursera/valgrind-aesdsocket.txt ./aesdsocket