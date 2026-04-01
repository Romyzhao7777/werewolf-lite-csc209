/*
 * Werewolf Lite server — single-threaded event loop with select().
 * TODO: lobby, role assignment, all phases, broadcasts, disconnect handling.
 */
#include "game.h"
#include "protocol.h"

#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define RECV_BUF_SIZE 4096

typedef struct {
    char data[RECV_BUF_SIZE];
    size_t len;
} RecvBuf;

static GameState g_state;
static RecvBuf g_recv[MAX_PLAYERS];

/* Send all bytes (handles partial send). Returns 0 on success, -1 on error. */
static int send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

/* Send one text line with '\n' at the end. */
static int send_line(int fd, const char *line) {
    if (!line) {
        return -1;
    }
    char buf[MAX_LINE_LEN + 2];
    size_t len = strlen(line);
    if (len > MAX_LINE_LEN) {
        return -1;
    }
    memcpy(buf, line, len);
    buf[len] = '\n';
    buf[len + 1] = '\0';
    return send_all(fd, buf, len + 1);
}

/* Create listening TCP socket on port; returns fd or -1. */
static int open_listen_socket(unsigned short port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    int opt = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, 8) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* TODO: void broadcast_line(const char *line); */

static void send_to_slot(int slot, const char *line) {
    if (slot >= 0 && slot < MAX_PLAYERS && g_state.players[slot].slot_used &&
        g_state.players[slot].fd >= 0) {
        (void)send_line(g_state.players[slot].fd, line);
    }
}

static void send_error(int slot, const char *msg) {
    char buf[MAX_LINE_LEN];
    snprintf(buf, sizeof(buf), "%s %s", MSG_ERROR, msg);
    send_to_slot(slot, buf);
}

static int find_free_slot(void) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!g_state.players[i].slot_used) {
            return i;
        }
    }
    return -1;
}

static void disconnect_slot(int slot) {
    /* TODO: notify others if game in progress, then cleanup */
    if (slot >= 0 && slot < MAX_PLAYERS && g_state.players[slot].fd >= 0) {
        close(g_state.players[slot].fd);
    }
    if (slot >= 0 && slot < MAX_PLAYERS) {
        g_state.players[slot].fd = -1;
        g_state.players[slot].slot_used = false;
        g_state.players[slot].has_name = false;
        g_state.players[slot].name[0] = '\0';
        g_recv[slot].len = 0;
    }
}

static void handle_client_line(int slot, char *line) {
    /* strip trailing CR/LF */
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n')) {
        line[--n] = '\0';
    }

    switch (g_state.phase) {
    case PHASE_LOBBY:
        /* TODO: parse NAME <username>, enforce uniqueness, broadcast WAITING n/4, start at 4 */
        send_error(slot, ERR_NOT_IMPLEMENTED);
        break;
    case PHASE_ROLE_ASSIGN:
        /* TODO */
        break;
    case PHASE_NIGHT:
        /* TODO: KILL <name> from werewolf only */
        break;
    case PHASE_DAY_ANNOUNCE:
        /* TODO */
        break;
    case PHASE_STATEMENT:
        /* TODO: SAY ... from current speaker only */
        break;
    case PHASE_VOTING:
        /* TODO: VOTE <name> */
        break;
    case PHASE_WIN_CHECK:
        /* TODO */
        break;
    case PHASE_GAME_OVER:
        break;
    default:
        send_error(slot, ERR_INVALID_COMMAND);
        break;
    }
}

static void try_read_client(int slot) {
    int fd = g_state.players[slot].fd;
    if (fd < 0) {
        return;
    }
    char tmp[512];
    ssize_t r = read(fd, tmp, sizeof(tmp));
    if (r < 0) {
        if (errno == EINTR) {
            return;
        }
        disconnect_slot(slot);
        return;
    }
    if (r == 0) {
        disconnect_slot(slot);
        return;
    }
    RecvBuf *b = &g_recv[slot];
    for (ssize_t i = 0; i < r; i++) {
        if (b->len >= RECV_BUF_SIZE - 1) {
            b->len = 0;
            send_error(slot, ERR_LINE_TOO_LONG);
            continue;
        }
        b->data[b->len++] = tmp[i];
    }
    b->data[b->len] = '\0';

    char *nl;
    while ((nl = memchr(b->data, '\n', b->len)) != NULL) {
        *nl = '\0';
        char line[MAX_LINE_LEN];
        strncpy(line, b->data, sizeof(line) - 1);
        line[sizeof(line) - 1] = '\0';
        size_t rest = b->len - (size_t)(nl - b->data) - 1;
        memmove(b->data, nl + 1, rest);
        b->len = rest;
        b->data[b->len] = '\0';
        handle_client_line(slot, line);
    }
}

int main(int argc, char **argv) {
    unsigned short port = DEFAULT_PORT;
    if (argc >= 2) {
        port = (unsigned short)atoi(argv[1]);
    }

    game_init(&g_state);
    srand((unsigned)time(NULL)); /* once per process for game_assign_roles / 进程内只播一次种 */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        g_recv[i].len = 0;
    }

    int listen_fd = open_listen_socket(port);
    if (listen_fd < 0) {
        perror("listen");
        return 1;
    }
    printf("Werewolf Lite server (starter) listening on port %u\n", (unsigned)port);

    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = listen_fd;
        FD_SET(listen_fd, &rfds);

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (g_state.players[i].slot_used && g_state.players[i].fd >= 0) {
                FD_SET(g_state.players[i].fd, &rfds);
                if (g_state.players[i].fd > maxfd) {
                    maxfd = g_state.players[i].fd;
                }
            }
        }

        int rv = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (rv < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            break;
        }

        if (FD_ISSET(listen_fd, &rfds)) {
            struct sockaddr_in peer;
            socklen_t plen = sizeof(peer);
            int cfd = accept(listen_fd, (struct sockaddr *)&peer, &plen);
            if (cfd >= 0) {
                int s = find_free_slot();
                if (s < 0) {
                    send_line(cfd, "ERROR Server full");
                    close(cfd);
                } else {
                    g_state.players[s].fd = cfd;
                    g_state.players[s].slot_used = true;
                    g_state.players[s].has_name = false;
                    g_recv[s].len = 0;
                    send_line(cfd, MSG_WELCOME);
                    /* TODO: wait for NAME, etc. */
                }
            }
        }

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!g_state.players[i].slot_used || g_state.players[i].fd < 0) {
                continue;
            }
            if (FD_ISSET(g_state.players[i].fd, &rfds)) {
                try_read_client(i);
            }
        }
    }

    close(listen_fd);
    return 0;
}
