# Werewolf Lite — CSC209 A3: expose PORT (default 4242) for socket projects.
PORT ?= 4242
CC = gcc
UNAME_S := $(shell uname -s)

# Base flags for all systems
CFLAGS = -Wall -Wextra -g -std=c11 -DDEFAULT_PORT=$(PORT)

# System-specific flags
ifeq ($(UNAME_S),Darwin)
    # macOS
    CFLAGS += -D_POSIX_C_SOURCE=200809L
else ifeq ($(UNAME_S),Linux)
    # Linux
    CFLAGS += -D_POSIX_C_SOURCE=200809L
else ifeq ($(OS),Windows_NT)
    # Windows (MSYS/Cygwin)
    CFLAGS += -D_POSIX_C_SOURCE=200809L
else
    # Fallback for other systems
    CFLAGS += -D_POSIX_C_SOURCE=200809L
endif

.PHONY: all clean

all: server client

server: server.c game.c game.h protocol.h
	$(CC) $(CFLAGS) -o server server.c game.c

client: client.c protocol.h
	$(CC) $(CFLAGS) -o client client.c

clean:
	rm -f server client
