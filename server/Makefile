# Makefile for aesdsocket

CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -Wall -Werror
CROSS_COMPILE ?=
TARGET ?= aesdsocket
LDFLAGS ?= -lpthread -lrt

default: $(TARGET)

all: default

aesdsocket: aesdsocket.o
	$(CC) $(CFLAGS) -o $(TARGET) aesdsocket.o $(LDFLAGS)
    
aesdsocket.o: aesdsocket.c
	$(CC) $(CFLAGS) -c aesdsocket.c -o aesdsocket.o
    
clean:
	rm -f $(TARGET) aesdsocket.o
