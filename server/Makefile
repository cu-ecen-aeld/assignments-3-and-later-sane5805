# Makefile for aesdsocket

CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Werror -g 
TARGET = aesdsocket

all: $(TARGET)

aesdsocket: aesdsocket.o
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.o

default: all

clean:
	rm -f *.o $(TARGET)
