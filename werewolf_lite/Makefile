# Werewolf Lite — build with gcc on Linux / macOS / WSL
CC = gcc
CFLAGS = -Wall -Wextra -g -std=c11 -D_POSIX_C_SOURCE=200809L

.PHONY: all clean

all: server client

server: server.c game.c game.h protocol.h
	$(CC) $(CFLAGS) -o server server.c game.c

client: client.c protocol.h
	$(CC) $(CFLAGS) -o client client.c

clean:
	rm -f server client
