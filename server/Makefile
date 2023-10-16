# Makefile for aesdsocket

CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -Wall -Werror
LDFLAG ?= -pthread -lrt 
TARGET ?= aesdsocket

all: aesdsocket

aesdsocket: aesdsocket.o
	$(CC) $(CFLAGS) -o aesdsocket.o -o $(TARGET) $(LDFLAGS)

aesdsocket.o: aesdsocket.c
	$(CC) $(CFLAGS) -c aesdsocket.c

default: all

clean:
	rm -f *.o aesdsocket
