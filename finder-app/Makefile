SRC := writer.c
TARGET = writer
CC = gcc

all: writer

writer: writer.c
	$(CROSS_COMPILE)$(CC) -o $(TARGET) $(SRC)

clean:
	-rm -f $(TARGET)
