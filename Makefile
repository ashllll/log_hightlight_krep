# krep - A high-performance string search utility
# Author: Davide Santangelo
# Version: 0.3.5

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

# Set to 0 to disable architecture-specific optimizations
ENABLE_ARCH_DETECTION ?= 1

CC = gcc
CFLAGS = -Wall -Wextra -O3 -std=c11 -pthread -D_GNU_SOURCE -D_DEFAULT_SOURCE
LDFLAGS = 

# Architecture-specific optimizations
ifeq ($(ENABLE_ARCH_DETECTION),1)
    # Detect OS first
    OS := $(shell uname -s)
    
    # Detect architecture type
    ARCH := $(shell uname -m)
    
    # Special case for PowerPC Macs which return "Power Macintosh" from uname -m
    ifeq ($(OS),Darwin)
        ifeq ($(ARCH),Power Macintosh)
            # Use uname -p instead which returns "powerpc"
            ARCH := $(shell uname -p)
        endif
    endif
    
    # x86/x86_64 uses -march=native
    ifneq (,$(filter x86_64 i386 i686,$(ARCH)))
        # Check for SIMD support on x86/x86_64
        ifeq ($(shell $(CC) -march=native -dM -E - < /dev/null 2>/dev/null | grep -q '__SSE4_2__' && echo yes),yes)
            CFLAGS += -msse4.2
        endif

        ifeq ($(shell $(CC) -march=native -dM -E - < /dev/null 2>/dev/null | grep -q '__AVX2__' && echo yes),yes)
            CFLAGS += -mavx2
        endif
    endif
    
    # PowerPC uses -mcpu=native
    ifneq (,$(filter ppc ppc64 powerpc powerpc64,$(ARCH)))
        # Check for SIMD support on PowerPC
        ifeq ($(shell $(CC) -mcpu=native -dM -E - < /dev/null 2>/dev/null | grep -q 'ALTIVEC' && echo yes),yes)
            CFLAGS += -maltivec
        endif
    endif
    
    # ARM detection
    ifneq (,$(filter arm arm64 aarch64,$(ARCH)))
        # Special case for Apple Silicon (macOS on ARM)
        ifneq (,$(filter Darwin,$(OS)))
            # Apple Silicon has NEON by default, no need for special flags
            # Just define the feature macro if needed
            CFLAGS += -D__ARM_NEON
        else
            # For other ARM platforms (Linux, etc.), try to use appropriate flags
            ifeq ($(shell $(CC) -mcpu=native -dM -E - < /dev/null 2>/dev/null | grep -q '__ARM_NEON' && echo yes),yes)
                # Different ARM platforms may use different flag syntax
                ifneq (,$(filter arm,$(ARCH)))
                    CFLAGS += -mfpu=neon
                endif
                # For arm64/aarch64, NEON is typically standard and doesn't need -mfpu
            endif
        endif
    endif
endif

SRC = krep.c
OBJ = $(SRC:.c=.o)
EXEC = krep

# Test files
TEST_SRC = test/test_krep.c test/test_regex.c
TEST_OBJ = $(TEST_SRC:.c=.o)
TEST_EXEC = test_krep

all: $(EXEC)

$(EXEC): $(OBJ)
	$(CC) $(CFLAGS) -o $(EXEC) $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Fix test target to use proper variables
test: CFLAGS += -DTESTING
test: $(OBJ) $(TEST_OBJ) 
	$(CC) $(CFLAGS) -o $(TEST_EXEC) $(TEST_OBJ) $(OBJ) $(LDFLAGS)
	./$(TEST_EXEC)

clean:
	rm -f $(OBJ) $(EXEC) $(TEST_OBJ) $(TEST_EXEC) krep_test.o

install: $(EXEC)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(EXEC) $(DESTDIR)$(BINDIR)/$(EXEC)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(EXEC)

.PHONY: all clean install uninstall test
