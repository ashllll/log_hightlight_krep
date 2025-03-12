# krep - A high-performance string search utility
# Author: Davide Santangelo
# Version: 0.1.6

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

CC = gcc
CFLAGS = -Wall -Wextra -O3 -std=c11 -pthread -D_GNU_SOURCE -D_DEFAULT_SOURCE
LDFLAGS =

# Check for SIMD support
ifeq ($(shell $(CC) -march=native -dM -E - < /dev/null | grep -q '__SSE4_2__' && echo yes),yes)
    CFLAGS += -msse4.2
endif

ifeq ($(shell $(CC) -march=native -dM -E - < /dev/null | grep -q '__AVX2__' && echo yes),yes)
    CFLAGS += -mavx2
endif

SRC = krep.c
OBJ = $(SRC:.c=.o)
EXEC = krep

# Test files
TEST_SRC = test/test_krep.c
TEST_OBJ = $(TEST_SRC:.c=.o)
TEST_EXEC = test_krep

all: $(EXEC)

$(EXEC): $(OBJ)
	$(CC) $(CFLAGS) -o $(EXEC) $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Test target
test: $(TEST_EXEC)
	./$(TEST_EXEC)

$(TEST_EXEC): $(TEST_OBJ) krep_test.o
	$(CC) $(CFLAGS) -o $(TEST_EXEC) $(TEST_OBJ) krep_test.o $(LDFLAGS)

krep_test.o: $(SRC)
	$(CC) $(CFLAGS) -DTESTING -c $(SRC) -o krep_test.o

clean:
	rm -f $(OBJ) $(EXEC) $(TEST_OBJ) $(TEST_EXEC)

install: $(EXEC)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(EXEC) $(DESTDIR)$(BINDIR)/$(EXEC)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(EXEC)

.PHONY: all clean install uninstall test
