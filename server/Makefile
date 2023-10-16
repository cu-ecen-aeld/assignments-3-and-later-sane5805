# Makefile for aesdsocket

CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Werror -g
LDFLAG ?= -pthread -lrt 
TARGET ?= aesdsocket

all: $(TARGET)

aesdsocket: aesdsocket.o
	$(CC) $(CFLAGS) $(LDFLAG) -o aesdsocket aesdsocket.o

default: all

clean:
	rm -f *.o $(TARGET)
