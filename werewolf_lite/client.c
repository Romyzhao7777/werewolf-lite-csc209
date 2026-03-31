/*
 * client.c — Werewolf Lite client (CSC209 A3, Category 2)
 *
 * Usage: ./client <host> [port]
 *
 * Code style matches CSC209 lecture examples:
 *   - getaddrinfo() to resolve hostname -> cast ai_addr to get sin_addr
 *   - Manual sockaddr_in setup + connect()
 *   - select() with FD_ZERO/FD_SET/FD_ISSET, manual numfd computation
 *   - Raw read()/write() with char buffers, newline-delimited lines
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "protocol.h"

/* -------------------------------------------------------------------------
 * Line-oriented read buffer
 *
 * TCP is a byte stream: one read() may return half a line or several lines.
 * We keep one LineBuf per fd and extract complete '\n'-terminated lines one
 * at a time, matching the class select() pattern from Image 3.
 * ---------------------------------------------------------------------- */
#define BUF_SIZE 4096

typedef struct {
    char data[BUF_SIZE];
    int  len;           /* bytes currently buffered */
} LineBuf;

/* buf_append: copy n bytes from src into buf.
 * Returns 0 on success, -1 on overflow. */
static int buf_append(LineBuf *buf, const char *src, int n) {
    if (buf->len + n >= BUF_SIZE) {
        return -1;
    }
    memcpy(buf->data + buf->len, src, n);
    buf->len += n;
    return 0;
}

/* buf_next_line: if a complete '\n'-terminated line exists, copy it
 * (without '\n', NUL-terminated) into out, remove it from buf, return 1.
 * Returns 0 if no complete line yet. */
static int buf_next_line(LineBuf *buf, char *out, int out_size) {
    char *nl = memchr(buf->data, '\n', buf->len);
    if (!nl) return 0;

    int line_len = (int)(nl - buf->data);
    int copy_len = (line_len < out_size - 1) ? line_len : out_size - 1;

    memcpy(out, buf->data, copy_len);
    out[copy_len] = '\0';

    /* Shift remaining bytes to front */
    int consumed = line_len + 1;   /* +1 to skip the '\n' */
    buf->len -= consumed;
    memmove(buf->data, buf->data + consumed, buf->len);

    return 1;
}

/* -------------------------------------------------------------------------
 * Minimal client state — tracks which prompt to show the player.
 * The server enforces all game rules; client only tracks display context.
 * ---------------------------------------------------------------------- */
typedef enum {
    STATE_LOBBY,          /* waiting for GAME_START              */
    STATE_NIGHT_WOLF,     /* wolf: must send KILL <name>         */
    STATE_NIGHT_WAIT,     /* villager: waiting for night to end  */
    STATE_DAY,            /* day phase                           */
    STATE_STATEMENT,      /* this player's turn to SAY           */
    STATE_STATEMENT_WAIT, /* another player is speaking          */
    STATE_VOTE,           /* must send VOTE <name>               */
    STATE_DONE            /* game over or aborted                */
} ClientState;

static ClientState g_state = STATE_LOBBY;

/* -------------------------------------------------------------------------
 * handle_server_line: parse one complete server line and display output.
 * Matches the first token against MSG_* constants from protocol.h.
 * ---------------------------------------------------------------------- */
static void handle_server_line(const char *line) {

    /* Does line start with keyword kw, followed by space or end-of-string? */
#define STARTS(kw) (strncmp(line, (kw), strlen(kw)) == 0 && \
                    (line[strlen(kw)] == '\0' || line[strlen(kw)] == ' '))

    /* Pointer to the argument part after "KEYWORD " */
#define ARG(kw) (line + strlen(kw) + (line[strlen(kw)] == ' ' ? 1 : 0))

    if (STARTS(MSG_WELCOME)) {
        printf("[Server] Connected! Type: NAME <your_name>\n> ");
        fflush(stdout);

    } else if (STARTS(MSG_WAITING)) {
        /* e.g. "WAITING 2/4 players" */
        printf("[Lobby] %s\n", ARG(MSG_WAITING));
        fflush(stdout);

    } else if (STARTS(MSG_GAME_START)) {
        g_state = STATE_DAY;
        printf("\n=== GAME START ===\n");
        fflush(stdout);

    } else if (STARTS(MSG_ROLE)) {
        /* "ROLE WEREWOLF"  or  "ROLE VILLAGER" */
        const char *role = ARG(MSG_ROLE);
        if (strcmp(role, ROLE_STR_WEREWOLF) == 0) {
            printf("[Private] Your role: WEREWOLF\n");
        } else {
            printf("[Private] Your role: VILLAGER\n");
        }
        fflush(stdout);

    } else if (STARTS(MSG_NIGHT_START)) {
        printf("\n--- NIGHT FALLS ---\n");
        fflush(stdout);

    } else if (STARTS(MSG_NIGHT_ACTION)) {
        /* Only the werewolf receives this */
        g_state = STATE_NIGHT_WOLF;
        printf("[Night] Choose a player to eliminate.\n");
        printf("[Night] Type: KILL <player_name>\n> ");
        fflush(stdout);

    } else if (STARTS(MSG_NIGHT_WAIT)) {
        g_state = STATE_NIGHT_WAIT;
        printf("[Night] Waiting for the werewolf...\n");
        fflush(stdout);

    } else if (STARTS(MSG_DAY_START)) {
        g_state = STATE_DAY;
        printf("\n--- DAY BREAKS ---\n");
        fflush(stdout);

    } else if (STARTS(MSG_PLAYER_ELIMINATED)) {
        /* "PLAYER_ELIMINATED Alice" */
        printf("[Game] %s was eliminated!\n", ARG(MSG_PLAYER_ELIMINATED));
        fflush(stdout);

    } else if (STARTS(MSG_ALIVE_PLAYERS)) {
        /* "ALIVE_PLAYERS Alice Bob Charlie" */
        printf("[Game] Players still alive: %s\n", ARG(MSG_ALIVE_PLAYERS));
        fflush(stdout);

    } else if (STARTS(MSG_YOUR_STATEMENT)) {
        g_state = STATE_STATEMENT;
        printf("[Discussion] Your turn to speak.\n");
        printf("[Discussion] Type: SAY <message>\n> ");
        fflush(stdout);

    } else if (STARTS(MSG_WAIT_STATEMENT)) {
        /* "WAIT_STATEMENT Alice is speaking" */
        g_state = STATE_STATEMENT_WAIT;
        printf("[Discussion] %s\n", ARG(MSG_WAIT_STATEMENT));
        fflush(stdout);

    } else if (STARTS(MSG_STATEMENT)) {
        /* "STATEMENT Alice: I think Bob is the wolf!" */
        printf("[Chat] %s\n", ARG(MSG_STATEMENT));
        fflush(stdout);

    } else if (STARTS(MSG_STATEMENT_PHASE_END)) {
        g_state = STATE_DAY;
        printf("[Game] Discussion phase ended.\n");
        fflush(stdout);

    } else if (STARTS(MSG_VOTE_PROMPT)) {
        g_state = STATE_VOTE;
        printf("[Vote] Cast your vote! Type: VOTE <player_name>\n> ");
        fflush(stdout);

    } else if (STARTS(MSG_VOTE_RESULT)) {
        /* "VOTE_RESULT Alice" */
        printf("[Vote] Village eliminated: %s\n", ARG(MSG_VOTE_RESULT));
        fflush(stdout);

    } else if (STARTS(MSG_VOTE_TIE)) {
        printf("[Vote] It's a tie — no one is eliminated.\n");
        fflush(stdout);

    } else if (STARTS(MSG_NO_PLAYER_ELIMINATED)) {
        printf("[Vote] No player was eliminated this round.\n");
        fflush(stdout);

    } else if (STARTS(MSG_GAME_OVER)) {
        /* "GAME_OVER VILLAGERS_WIN"  or  "GAME_OVER WEREWOLF_WIN" */
        g_state = STATE_DONE;
        const char *result = ARG(MSG_GAME_OVER);
        printf("\n=== GAME OVER ===\n");
        if (strcmp(result, WIN_STR_VILLAGERS) == 0) {
            printf("VILLAGERS WIN! The werewolf has been found.\n");
        } else if (strcmp(result, WIN_STR_WEREWOLF) == 0) {
            printf("WEREWOLF WINS! The village has fallen.\n");
        } else {
            printf("Result: %s\n", result);
        }
        fflush(stdout);

    } else if (STARTS(MSG_ERROR)) {
        printf("[Error] %s\n", ARG(MSG_ERROR));
        /* Re-show prompt so the player knows they can try again */
        if (g_state == STATE_LOBBY      ||
            g_state == STATE_NIGHT_WOLF ||
            g_state == STATE_STATEMENT  ||
            g_state == STATE_VOTE) {
            printf("> ");
        }
        fflush(stdout);

    } else if (STARTS(MSG_PLAYER_DISCONNECTED)) {
        printf("[Game] %s has disconnected.\n", ARG(MSG_PLAYER_DISCONNECTED));
        fflush(stdout);

    } else if (STARTS(MSG_GAME_ABORTED)) {
        g_state = STATE_DONE;
        printf("[Game] Game aborted — a player disconnected.\n");
        fflush(stdout);

    } else {
        /* Unknown — print raw so the player can see it */
        printf("[Server] %s\n", line);
        fflush(stdout);
    }

#undef STARTS
#undef ARG
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <host> [port]\n", argv[0]);
        exit(1);
    }

    const char *host = argv[1];
    int port = (argc == 3) ? atoi(argv[2]) : DEFAULT_PORT;

    /* ------------------------------------------------------------------
     * Step 1: Create the socket  (Image 2 style)
     * ------------------------------------------------------------------ */
    int soc = socket(AF_INET, SOCK_STREAM, 0);
    if (soc == -1) {
        perror("socket");
        exit(1);
    }

    /* ------------------------------------------------------------------
     * Step 2: Fill sockaddr_in; use getaddrinfo() to resolve the hostname
     *         then cast ai_addr to extract sin_addr  (Image 1 style)
     * ------------------------------------------------------------------ */
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    memset(&server.sin_zero, 0, 8);
    server.sin_port = htons(port);

    struct addrinfo *result;
    if (getaddrinfo(host, NULL, NULL, &result) != 0) {
        fprintf(stderr, "getaddrinfo: could not resolve %s\n", host);
        exit(1);
    }
    server.sin_addr = ((struct sockaddr_in *) result->ai_addr)->sin_addr;
    freeaddrinfo(result);

    /* ------------------------------------------------------------------
     * Step 3: Connect to the server  (Image 2 style)
     * ------------------------------------------------------------------ */
    if (connect(soc, (struct sockaddr *)&server, sizeof(struct sockaddr_in)) == -1) {
        perror("connect");
        exit(1);
    }

    printf("Connected to %s:%d\n", host, port);

    /* ------------------------------------------------------------------
     * Step 4: select() event loop  (Image 3 style)
     *
     * Two fds in the fd_set:
     *   soc          — server messages (newline-delimited text)
     *   STDIN_FILENO — keyboard input from the player
     *
     * Each fd has its own LineBuf to handle partial / multi-line reads.
     * ------------------------------------------------------------------ */
    LineBuf sock_buf, stdin_buf;
    memset(&sock_buf,  0, sizeof(sock_buf));
    memset(&stdin_buf, 0, sizeof(stdin_buf));

    char tmp[BUF_SIZE];        /* scratch buffer for raw read() bytes  */
    char line[MAX_LINE_LEN];   /* one extracted protocol line          */

    while (g_state != STATE_DONE) {

        /* Build fd_set — Image 3 style --------------------------------- */
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(soc,          &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);

        /* numfd = highest fd + 1 — Image 3 style */
        int numfd;
        if (soc > STDIN_FILENO) {
            numfd = soc + 1;
        } else {
            numfd = STDIN_FILENO + 1;
        }

        if (select(numfd, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            break;
        }

        /* Read from server — Image 3 FD_ISSET style -------------------- */
        if (FD_ISSET(soc, &read_fds)) {
            int r = read(soc, tmp, sizeof(tmp) - 1);
            if (r < 0) {
                perror("read");
                break;
            } else if (r == 0) {
                /* r == 0 means server closed the connection (Image 3 pattern) */
                printf("[Connection] Server disconnected.\n");
                break;
            } else {
                if (buf_append(&sock_buf, tmp, r) < 0) {
                    fprintf(stderr, "[Error] Server line too long, resetting.\n");
                    sock_buf.len = 0;
                }
                /* Drain all complete lines from the socket buffer */
                while (buf_next_line(&sock_buf, line, sizeof(line))) {
                    handle_server_line(line);
                }
            }
        }

        /* Read from stdin — Image 3 FD_ISSET style --------------------- */
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            int r = read(STDIN_FILENO, tmp, sizeof(tmp) - 1);
            if (r < 0) {
                perror("read");
                break;
            } else if (r == 0) {
                /* EOF (Ctrl-D) — send QUIT and exit cleanly */
                write(soc, CMD_QUIT "\n", strlen(CMD_QUIT) + 1);
                break;
            } else {
                if (buf_append(&stdin_buf, tmp, r) < 0) {
                    fprintf(stderr, "[Error] Input line too long, discarding.\n");
                    stdin_buf.len = 0;
                }
                /* Forward each complete line to the server */
                while (buf_next_line(&stdin_buf, line, sizeof(line))) {
                    if (line[0] == '\0') continue;    /* skip blank lines */
                    write(soc, line, strlen(line));   /* command text     */
                    write(soc, "\n", 1);              /* protocol newline */
                }
            }
        }
    }

    close(soc);
    return 0;
}