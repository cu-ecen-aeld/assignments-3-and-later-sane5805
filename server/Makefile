# Makefile for aesdsocket

CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Werror -g

CROSS_COMPILE ?=

LDFLAG ?= -pthread -lrt 
TARGET ?= aesdsocket

all: $(TARGET)

aesdsocket: aesdsocket.o
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.o $(LDFLAG)

default: all

clean:
	rm -f *.o $(TARGET)
