# Werewolf Lite — CSC209 A3: expose PORT (default 4242) for socket projects.
PORT ?= 4242
CC = gcc
CFLAGS = -Wall -Wextra -g -std=c11 -D_POSIX_C_SOURCE=200809L -DDEFAULT_PORT=$(PORT)

.PHONY: all clean

all: server client

server: server.c game.c game.h protocol.h
	$(CC) $(CFLAGS) -o server server.c game.c

client: client.c protocol.h
	$(CC) $(CFLAGS) -o client client.c

clean:
	rm -f server client
