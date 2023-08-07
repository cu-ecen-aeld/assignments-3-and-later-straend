.PHONY: all default clean valgrind

SRC := aesdsocket.c
TARGET ?= aesdsocket
#CC = ${CROSS_COMPILE}gcc
CFLAGS ?= -g -Wall -Werror -std=gnu99
LDFLAGS ?= -lpthread
all: default

default: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	-rm -f $(TARGET)

valgrind: aesdsocket
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=/tmp/valgrind-aesdsocket.txt ./aesdsocket
#	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=/home/ubuntu/coursera/valgrind-aesdsocket.txt ./aesdsocket