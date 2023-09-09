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

# Set execute permission for the writer binary
set-permissions:
	chmod +x $(TARGET)

# Clean target: Remove the writer application and object files
clean:
	rm -f $(TARGET) $(OBJ)

# Cross-compilation support
ifeq ($(CROSS_COMPILE),aarch64-none-linux-gnu-)
CC := $(CROSS_COMPILE)gcc
endif

# New target: Generate cross-compile.txt
cross-compile-info:
	aarch64-none-linux-gnu-gcc -print-sysroot -v > cross-compile.txt
	@echo "Cross-compile information has been saved to cross-compile.txt."

# Add the new target to the dependencies of "all" to generate cross-compile.txt when building
all: cross-compile-info
