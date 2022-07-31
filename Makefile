CC=gcc
CFLAGS=-Wall -Werror -Wvla -O0 -std=c11 -g -D TESTING
LDFLAGS=-lm
BINARIES=spx_exchange spx_trader spx_test_trader

all: $(BINARIES)

.PHONY: clean
clean:
	rm -f $(BINARIES)

