# Compiler settings
CC := $(CROSS_COMPILE)gcc
CFLAGS := -Wall -Werror
LDFLAGS := -lm
TARGET := writer

# Source and object files
SRC := writer.c
OBJ := $(SRC:.c=.o)

# Default target: Build the writer application
all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(TARGET) $(LDFLAGS)

# Clean target: Remove the writer application and object files
clean:
	rm -f $(TARGET) $(OBJ)

# Cross-compilation support
ifeq ($(CROSS_COMPILE),aarch64-none-linux-gnu-)
CC := $(CROSS_COMPILE)gcc
endif
