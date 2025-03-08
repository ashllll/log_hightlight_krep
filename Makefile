# krep - A high-performance string search utility
# Author: Davide Santangelo
# Version: 0.1.0

CC = gcc
CFLAGS = -Wall -Wextra -O3 -std=c11 -pthread
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

all: $(EXEC)

$(EXEC): $(OBJ)
	$(CC) $(CFLAGS) -o $(EXEC) $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(EXEC)

install: $(EXEC)
	install -m 755 $(EXEC) /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/$(EXEC)

.PHONY: all clean install uninstall
