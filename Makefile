# krep - A high-performance string search utility
# Author: Davide Santangelo
# Version: 0.4.2

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

# Set to 0 to disable architecture-specific optimizations
ENABLE_ARCH_DETECTION ?= 1

CC = gcc
# Base CFLAGS (without testing or specific arch flags initially)
BASE_CFLAGS = -Wall -Wextra -O3 -std=c11 -pthread -D_GNU_SOURCE -D_DEFAULT_SOURCE
LDFLAGS =

# --- Architecture Detection (Keep as is) ---
ARCH_CFLAGS =
ifeq ($(ENABLE_ARCH_DETECTION),1)
    OS := $(shell uname -s)
    ARCH := $(shell uname -m)
    ifeq ($(OS),Darwin)
        ifeq ($(ARCH),Power Macintosh)
            ARCH := $(shell uname -p)
        endif
    endif
    ifneq (,$(filter x86_64 i386 i686,$(ARCH)))
        ifeq ($(shell $(CC) -march=native -dM -E - < /dev/null 2>/dev/null | grep -q '__SSE4_2__' && echo yes),yes)
            ARCH_CFLAGS += -msse4.2
        endif
        ifeq ($(shell $(CC) -march=native -dM -E - < /dev/null 2>/dev/null | grep -q '__AVX2__' && echo yes),yes)
            ARCH_CFLAGS += -mavx2
        endif
    endif
    ifneq (,$(filter ppc ppc64 powerpc powerpc64,$(ARCH)))
        ifeq ($(shell $(CC) -mcpu=native -dM -E - < /dev/null 2>/dev/null | grep -q 'ALTIVEC' && echo yes),yes)
            ARCH_CFLAGS += -maltivec
        endif
    endif
    ifneq (,$(filter arm arm64 aarch64,$(ARCH)))
        # Define __ARM_NEON if detected or assumed (like on Apple Silicon)
        NEON_DETECTED=no
        ifeq ($(OS),Darwin)
            # Assume NEON on Apple Silicon/macOS ARM
            NEON_DETECTED=yes
        else
            # Check generic ARM
             ifeq ($(shell $(CC) -mcpu=native -dM -E - < /dev/null 2>/dev/null | grep -q '__ARM_NEON' && echo yes),yes)
                NEON_DETECTED=yes
                # Add specific FPU flags only if needed (usually not for arm64)
                # ifneq (,$(filter arm,$(ARCH)))
                #    ARCH_CFLAGS += -mfpu=neon
                # endif
            endif
        endif
        # Add the define if NEON is detected/assumed
        ifeq ($(NEON_DETECTED),yes)
             ARCH_CFLAGS += -D__ARM_NEON
        endif
    endif
endif
# --- End Arch Detection ---

# Combine base and arch flags for normal compilation
CFLAGS = $(BASE_CFLAGS) $(ARCH_CFLAGS)

# Specific flags for testing compilation
TEST_CFLAGS = $(CFLAGS) -DTESTING

SRC = krep.c
OBJ = $(SRC:.c=.o) # krep.o (for main executable)
EXEC = krep

# Test source files and their object files
TEST_SRC_FILES = test/test_krep.c test/test_regex.c
TEST_OBJ = $(TEST_SRC_FILES:.c=.o) # test/test_krep.o test/test_regex.o
TEST_EXEC = test_krep

# Define a separate object file for krep.c when compiled for testing
KREP_TEST_OBJ = krep_test.o

# Default target
all: $(EXEC)

# Rule to build the main executable
$(EXEC): $(OBJ)
	$(CC) $(CFLAGS) -o $(EXEC) $(OBJ) $(LDFLAGS)

# Rule to build the main krep.o (uses CFLAGS, includes main)
$(OBJ): %.o: %.c krep.h
	$(CC) $(CFLAGS) -c $< -o $@

# Rule to build test object files (uses TEST_CFLAGS)
$(TEST_OBJ): test/%.o: test/%.c test/test_krep.h test/test_compat.h krep.h
	$(CC) $(TEST_CFLAGS) -c $< -o $@

# Rule to build krep.c specifically *for testing* (uses TEST_CFLAGS, excludes main)
$(KREP_TEST_OBJ): krep.c krep.h
	$(CC) $(TEST_CFLAGS) -c $< -o $@

# Rule to build the test executable (links test objects + krep_test.o)
$(TEST_EXEC): $(KREP_TEST_OBJ) $(TEST_OBJ)
	$(CC) $(TEST_CFLAGS) -o $(TEST_EXEC) $^ $(LDFLAGS)

# Target to run tests
test: $(TEST_EXEC)
	./$(TEST_EXEC)

clean:
	rm -f $(OBJ) $(EXEC) $(TEST_OBJ) $(TEST_EXEC) $(KREP_TEST_OBJ)

install: $(EXEC)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(EXEC) $(DESTDIR)$(BINDIR)/$(EXEC)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(EXEC)

.PHONY: all clean install uninstall test
