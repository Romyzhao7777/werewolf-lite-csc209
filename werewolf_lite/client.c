/*
 * Werewolf Lite client — TCP connection and line-based I/O.
 * TODO: read server prompts, send NAME, KILL, SAY, VOTE; use select() on socket + stdin.
 */
#include "protocol.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv) {
    const char *host = "127.0.0.1";
    unsigned short port = DEFAULT_PORT;
    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        port = (unsigned short)atoi(argv[2]);
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid host: %s\n", host);
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    printf("Connected to %s:%u\n", host, (unsigned)port);
    /* TODO: read WELCOME, prompt for name, send "NAME <name>" with send() + "\\n" */
    /* TODO: loop with select(sock, stdin) to print server lines and send user commands */

    close(sock);
    return 0;
}
