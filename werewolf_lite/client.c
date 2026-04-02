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

#ifndef MSG_WAIT_VOTE
#define MSG_WAIT_VOTE "WAIT_VOTE"
#endif

/* -------------------------------------------------------------------------
 * Line-oriented read buffer
 * ---------------------------------------------------------------------- */
#define BUF_SIZE 4096

typedef struct {
    char data[BUF_SIZE];
    int  len;
} LineBuf;

/* buf_append: copy n bytes from src into buf. Returns 0 ok, -1 overflow. */
static int buf_append(LineBuf *buf, const char *src, int n) {
    if (buf->len + n >= BUF_SIZE) return -1;
    memcpy(buf->data + buf->len, src, n);
    buf->len += n;
    return 0;
}

/*
 * buf_next_line: if a '\n'-terminated line exists, copy it (without '\n',
 * NUL-terminated) into out, remove it from buf, return 1.
 * Returns 0 if no complete line yet.
 */
static int buf_next_line(LineBuf *buf, char *out, int out_size) {
    char *nl = memchr(buf->data, '\n', buf->len);
    if (!nl) return 0;

    int line_len = (int)(nl - buf->data);
    int copy_len = (line_len < out_size - 1) ? line_len : out_size - 1;
    memcpy(out, buf->data, copy_len);
    out[copy_len] = '\0';

    int consumed = line_len + 1;   /* +1 to consume the '\n' itself */
    buf->len -= consumed;
    memmove(buf->data, buf->data + consumed, buf->len);
    return 1;
}

/* ----------------------------------------------------------------------
 * Client State
 * ---------------------------------------------------------------------- */
typedef enum {
    STATE_LOBBY,           /* waiting for GAME_START; NAME expected  */
    STATE_NIGHT_WOLF,      /* werewolf: must send KILL <name>        */
    STATE_NIGHT_WAIT,      /* villager: waiting for wolf to act      */
    STATE_DAY,             /* day announce phase, no input           */
    STATE_STATEMENT,       /* this player's turn: send SAY <text>    */
    STATE_STATEMENT_WAIT,  /* another player is speaking             */
    STATE_VOTE,            /* must send VOTE <name>                  */
    STATE_VOTE_WAIT,       /* dead player waiting during voting      */
} ClientState;

static ClientState g_state = STATE_LOBBY;


static void handle_server_line(const char *line) {

    /*
     * STARTS(kw): true if line begins with exactly the keyword kw,
     * followed by end-of-string or a space.
     */
#define STARTS(kw) (strncmp(line, (kw), strlen(kw)) == 0 && \
                    (line[strlen(kw)] == '\0' || line[strlen(kw)] == ' '))

    /* ARG(kw): pointer to the first character after "KEYWORD " */
#define ARG(kw) (line + strlen(kw) + (line[strlen(kw)] == ' ' ? 1 : 0))

    /* ==================================================================
     * WELCOME
     * Sent: (1) when a new client first connects
     *       (2) by reset_to_lobby() after GAME_OVER or GAME_ABORTED
     * Action: always reset to lobby state so the player can re-register.
     * ================================================================== */
    if (STARTS(MSG_WELCOME)) {
        g_state = STATE_LOBBY;
        printf("\n[Server] Welcome! Enter your name: NAME <your_name>\n> ");
        fflush(stdout);

    /* ==================================================================
     * WAITING <n>/<MAX> players
     * Sent: broadcast whenever a player registers a name in the lobby.
     * ================================================================== */
    } else if (STARTS(MSG_WAITING)) {
        printf("[Lobby] %s\n", ARG(MSG_WAITING));
        fflush(stdout);

    /* ==================================================================
     * GAME_START
     * Sent: broadcast when MIN_PLAYERS have all registered names.
     * Immediately followed by ROLE (private) then NIGHT_START.
     * ================================================================== */
    } else if (STARTS(MSG_GAME_START)) {
        g_state = STATE_DAY;   /* neutral day state until ROLE/NIGHT arrive */
        printf("\n=== GAME START ===\n");
        fflush(stdout);

    /* ==================================================================
     * ROLE WEREWOLF | ROLE VILLAGER
     * Sent: private, immediately after GAME_START.
     * ================================================================== */
    } else if (STARTS(MSG_ROLE)) {
        const char *role = ARG(MSG_ROLE);
        if (strcmp(role, ROLE_STR_WEREWOLF) == 0) {
            printf("[Private] Your role: WEREWOLF\n");
        } else {
            printf("[Private] Your role: VILLAGER\n");
        }
        fflush(stdout);

    /* ==================================================================
     * NIGHT_START
     * Sent: broadcast to all at the start of night.
     * Followed by NIGHT_ACTION (wolf only) or NIGHT_WAIT (villagers).
     * ================================================================== */
    } else if (STARTS(MSG_NIGHT_ACTION)) {
        g_state = STATE_NIGHT_WOLF;
        printf("\n--- NIGHT FALLS ---\n");
        printf("[Night] Choose a player to eliminate.\n");
        printf("[Night] Type: KILL <player_name>\n> ");
        fflush(stdout);

    } else if (STARTS(MSG_NIGHT_WAIT)) {
        g_state = STATE_NIGHT_WAIT;
        printf("\n--- NIGHT FALLS ---\n");
        printf("[Night] Waiting for the werewolf...\n");
        fflush(stdout);

    /* ==================================================================
     * DAY_START
     * Sent: broadcast at the start of the day announce phase.
     * Immediately followed by PLAYER_ELIMINATED then ALIVE_PLAYERS.
     * ================================================================== */
    } else if (STARTS(MSG_DAY_START)) {
        g_state = STATE_DAY;
        printf("\n--- DAY BREAKS ---\n");
        fflush(stdout);

    /* ==================================================================
     * PLAYER_ELIMINATED <name>
     * Sent: broadcast in start_day_announce_phase() if night_victim >= 0.
     * ================================================================== */
    } else if (STARTS(MSG_PLAYER_ELIMINATED)) {
        printf("[Game] %s was eliminated during the night!\n",
               ARG(MSG_PLAYER_ELIMINATED));
        fflush(stdout);

    /* ==================================================================
     * ALIVE_PLAYERS <name1> <name2> ...
     * Sent: broadcast after elimination announcements.
     * ================================================================== */
    } else if (STARTS(MSG_ALIVE_PLAYERS)) {
        printf("[Game] Players still alive: %s\n", ARG(MSG_ALIVE_PLAYERS));
        fflush(stdout);

    /* ==================================================================
     * YOUR_STATEMENT
     * Sent: private to the current speaker in prompt_statement_turn().
     * ================================================================== */
    } else if (STARTS(MSG_YOUR_STATEMENT)) {
        g_state = STATE_STATEMENT;
        printf("[Discussion] It's your turn to speak.\n");
        printf("[Discussion] Type: SAY <message>\n> ");
        fflush(stdout);

    /* ==================================================================
     * WAIT_STATEMENT <name> is speaking
     * Sent: to all non-speakers in prompt_statement_turn().
     * Check before STATEMENT so "WAIT_STATEMENT" doesn't match "STATEMENT".
     * ================================================================== */
    } else if (STARTS(MSG_WAIT_STATEMENT)) {
        g_state = STATE_STATEMENT_WAIT;
        printf("[Discussion] %s\n", ARG(MSG_WAIT_STATEMENT));
        fflush(stdout);

    /* ==================================================================
     * STATEMENT <name>: <text>
     * Sent: broadcast when a player successfully uses SAY.
     * ================================================================== */
    } else if (STARTS(MSG_STATEMENT)) {
        printf("[Chat] %s\n", ARG(MSG_STATEMENT));
        fflush(stdout);

    /* ==================================================================
     * STATEMENT_PHASE_END
     * Sent: broadcast when the last speaker has spoken.
     * Immediately followed by VOTE_PROMPT (alive) or WAIT_VOTE (dead).
     * ================================================================== */
    } else if (STARTS(MSG_STATEMENT_PHASE_END)) {
        g_state = STATE_DAY;
        printf("[Game] Discussion phase ended.\n");
        fflush(stdout);

    /* ==================================================================
     * VOTE_PROMPT
     * Sent: in start_voting_phase() to alive players.
     * ================================================================== */
    } else if (STARTS(MSG_VOTE_PROMPT)) {
        g_state = STATE_VOTE;
        printf("[Vote] Cast your vote! Type: VOTE <player_name>\n> ");
        fflush(stdout);

    /* ==================================================================
     * WAIT_VOTE Players are voting
     * Sent: in start_voting_phase() to DEAD players
     * ================================================================== */
    } else if (STARTS(MSG_WAIT_VOTE)) {
        g_state = STATE_VOTE_WAIT;
        printf("[Vote] Players are voting. Please wait...\n");
        fflush(stdout);

    /* ==================================================================
     * VOTE_RESULT <name>
     * Sent: broadcast after tallying, when there is a clear winner.
     * ================================================================== */
    } else if (STARTS(MSG_VOTE_RESULT)) {
        printf("[Vote] The village has voted to eliminate: %s\n",
               ARG(MSG_VOTE_RESULT));
        fflush(stdout);

    /* ==================================================================
     * VOTE_TIE
     * Sent: broadcast when votes are tied.
     * ================================================================== */
    } else if (STARTS(MSG_VOTE_TIE)) {
        g_state = STATE_DAY;
        printf("[Vote] It's a tie! Restarting discussion...\n");
        fflush(stdout);

    /* ==================================================================
     * NO_PLAYER_ELIMINATED
     * Sent: if the server ever uses this (currently server uses VOTE_TIE).
     * ================================================================== */
    } else if (STARTS(MSG_NO_PLAYER_ELIMINATED)) {
        printf("[Vote] No player was eliminated this round.\n");
        fflush(stdout);

    /* ==================================================================
     * GAME_OVER VILLAGERS_WIN | GAME_OVER WEREWOLF_WIN
     * Sent: broadcast, then server IMMEDIATELY calls reset_to_lobby()
     * which sends WELCOME to all remaining clients.
     * ================================================================== */
    } else if (STARTS(MSG_GAME_OVER)) {
        const char *result = ARG(MSG_GAME_OVER);
        printf("\n=== GAME OVER ===\n");
        if (strcmp(result, WIN_STR_VILLAGERS) == 0) {
            printf("VILLAGERS WIN! The werewolf has been found.\n");
        } else if (strcmp(result, WIN_STR_WEREWOLF) == 0) {
            printf("WEREWOLF WINS! The village has fallen.\n");
        } else {
            printf("Result: %s\n", result);
        }
        printf("[Game] Returning to lobby...\n");
        fflush(stdout);

    /* ==================================================================
     * PLAYER_DISCONNECTED <name>
     * Sent: broadcast when a player disconnects mid-game.
     * Followed immediately by GAME_ABORTED.
     * ================================================================== */
    } else if (STARTS(MSG_PLAYER_DISCONNECTED)) {
        printf("[Game] %s has disconnected.\n", ARG(MSG_PLAYER_DISCONNECTED));
        fflush(stdout);

    /* ==================================================================
     * GAME_ABORTED
     * Sent: broadcast after PLAYER_DISCONNECTED during a game.
     * server.c then calls reset_to_lobby() which sends WELCOME to
     * remaining clients — same as GAME_OVER, do NOT exit.
     * ================================================================== */
    } else if (STARTS(MSG_GAME_ABORTED)) {
        printf("[Game] Game aborted due to disconnection.\n");
        printf("[Game] Returning to lobby...\n");
        fflush(stdout);

    /* ==================================================================
     * ERROR <reason>
     * Sent: private, for any invalid command the server rejects.
     * Re-show the prompt so the player knows they can try again.
     * ================================================================== */
    } else if (STARTS(MSG_ERROR)) {
        printf("[Error] %s\n", ARG(MSG_ERROR));
        if (g_state == STATE_LOBBY) {
            printf("> ");
        } else if (g_state == STATE_NIGHT_WOLF) {
            printf("[Night] Type: KILL <player_name>\n> ");
        } else if (g_state == STATE_STATEMENT) {
            printf("[Discussion] Type: SAY <message>\n> ");
        } else if (g_state == STATE_VOTE) {
            printf("[Vote] Type: VOTE <player_name>\n> ");
        }
        fflush(stdout);

    /* ==================================================================
     * Unknown message — print raw so nothing is silently lost.
     * ================================================================== */
    } else {
        printf("[Server] %s\n", line);
        fflush(stdout);
    }

#undef STARTS
#undef ARG
}


int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <host> [port]\n", argv[0]);
        exit(1);
    }

    const char *host = argv[1];
    int port = DEFAULT_PORT;

    if (argc == 3) {
        port = atoi(argv[2]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[2]);
            exit(1);
        }
    }

    // Step 1: Create socket
    int soc = socket(AF_INET, SOCK_STREAM, 0);
    if (soc == -1) {
        perror("socket");
        exit(1);
    }

    struct addrinfo hints, *result;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &result) != 0) {
        fprintf(stderr, "getaddrinfo failed\n");
        exit(1);
    }

    // Step 2: Connect
    if (connect(soc, result->ai_addr, result->ai_addrlen) == -1) {
        perror("connect");
        exit(1);
    }

    freeaddrinfo(result);

    printf("Connected to %s:%d\n", host, port);

    // Step 3: Select
    LineBuf sock_buf, stdin_buf;
    memset(&sock_buf,  0, sizeof(sock_buf));
    memset(&stdin_buf, 0, sizeof(stdin_buf));

    char tmp[BUF_SIZE];        
    char line[MAX_LINE_LEN];   

    while (1) {   // exit only when server closes connection

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(soc,          &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);

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

        // data from server
        if (FD_ISSET(soc, &read_fds)) {
            int r = read(soc, tmp, sizeof(tmp) - 1);
            if (r < 0) {
                perror("read");
                break;
            } else if (r == 0) {
                printf("[Connection] Server disconnected.\n");
                break;
            } else {
                if (buf_append(&sock_buf, tmp, r) < 0) {
                    fprintf(stderr, "[Error] Server line too long, resetting buffer.\n");
                    sock_buf.len = 0;
                }
                while (buf_next_line(&sock_buf, line, sizeof(line))) {
                    handle_server_line(line);
                }
            }
        }

        // input from stdin
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            int r = read(STDIN_FILENO, tmp, sizeof(tmp) - 1);
            if (r < 0) {
                perror("read");
                break;
            } else if (r == 0) {
                write(soc, CMD_QUIT "\n", strlen(CMD_QUIT) + 1);
                break;
            } else {
                if (buf_append(&stdin_buf, tmp, r) < 0) {
                    fprintf(stderr, "[Error] Input line too long, discarding.\n");
                    stdin_buf.len = 0;
                }
                while (buf_next_line(&stdin_buf, line, sizeof(line))) {
                    if (line[0] == '\0') continue;  // skip blank line

                    if (strcmp(line, "exit") == 0) {
                        printf("[Game] You have left the game.\n");
                        close(soc);
                        exit(0);
                    }

                    char outbuf[MAX_LINE_LEN + 1];
                    int outlen = snprintf(outbuf, sizeof(outbuf), "%s\n", line);
                    write(soc, outbuf, outlen);

                    if (g_state == STATE_NIGHT_WOLF ||
                        g_state == STATE_STATEMENT  ||
                        g_state == STATE_VOTE) {
                        g_state = STATE_DAY;
                    }
                }
            }
        }
    }

    close(soc);
    return 0;
}