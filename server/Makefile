# Makefile for aesdsocket

CC = gcc
CFLAGS = -g -Wall -Werror
TARGET = aesdsocket

all: $(TARGET)

$(TARGET): aesdsocket.c
	$(CC) $(CFLAGS) -o $(TARGET) aesdsocket.c

default: all

clean:
	rm -f $(TARGET)
