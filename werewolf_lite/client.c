/*
 * client.c — Werewolf Lite client (CSC209 A3, Category 2)
 *
 * Usage: ./client <host> [port]
 *
 * Carefully matched against server.c:
 *   - MSG_GAME_OVER and MSG_GAME_ABORTED both trigger reset_to_lobby() on
 *     the server, so the client must return to STATE_LOBBY (not exit).
 *   - MSG_WAIT_VOTE is sent by start_voting_phase() to dead players.
 *   - MSG_WELCOME is re-sent after every game end; client resets state on it.
 *   - MSG_NIGHT_ACTION / MSG_NIGHT_WAIT are sent as full verbatim strings.
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

/*
 * MSG_WAIT_VOTE is used in server.c's start_voting_phase() for dead players:
 *   snprintf(msg, ..., "%s Players are voting\n", MSG_WAIT_VOTE);
 * but it is NOT defined in protocol.h, so we define it here.
 */
#ifndef MSG_WAIT_VOTE
#define MSG_WAIT_VOTE "WAIT_VOTE"
#endif

/* -------------------------------------------------------------------------
 * Line-oriented read buffer
 *
 * TCP is a byte stream: one read() may return half a line or several lines.
 * We keep one LineBuf per fd and extract '\n'-terminated lines one at a time.
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

/* -------------------------------------------------------------------------
 * Client state — tracks what phase we are in for display/prompt purposes.
 *
 * KEY INSIGHT from server.c:
 *   After GAME_OVER or GAME_ABORTED, the server calls reset_to_lobby() which
 *   re-sends WELCOME to every still-connected client.  So we must NOT exit on
 *   GAME_OVER/GAME_ABORTED — instead we go back to STATE_LOBBY and wait for
 *   the WELCOME that follows immediately.
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

/* -------------------------------------------------------------------------
 * handle_server_line: parse one complete server line and react.
 *
 * Every message the server can send is handled here.  The order of the
 * if-else chain matters when one keyword is a prefix of another — longest
 * match first (e.g. NIGHT_ACTION before NIGHT_, WAIT_STATEMENT before WAIT_).
 * ---------------------------------------------------------------------- */
static void handle_server_line(const char *line) {

    /*
     * STARTS(kw): true if line begins with exactly the keyword kw,
     * followed by end-of-string or a space.
     * This is safe even when kw contains spaces (MSG_NIGHT_ACTION,
     * MSG_NIGHT_WAIT) because strncmp checks the entire kw string.
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
     *
     * NOTE: check NIGHT_ACTION and NIGHT_WAIT before NIGHT_START because
     * all three start with "NIGHT" — longest-specific match wins.
     * ================================================================== */
    } else if (STARTS(MSG_NIGHT_ACTION)) {
        /*
         * server.c sends exactly:
         *   snprintf(msg, sizeof(msg), "%s\n", MSG_NIGHT_ACTION);
         * where MSG_NIGHT_ACTION = "NIGHT_ACTION Choose a player to eliminate"
         * So the whole line IS the keyword string. STARTS() still matches.
         */
        g_state = STATE_NIGHT_WOLF;
        printf("\n--- NIGHT FALLS ---\n");
        printf("[Night] Choose a player to eliminate.\n");
        printf("[Night] Type: KILL <player_name>\n> ");
        fflush(stdout);

    } else if (STARTS(MSG_NIGHT_WAIT)) {
        /*
         * server.c sends exactly:
         *   snprintf(msg, sizeof(msg), "%s\n", MSG_NIGHT_WAIT);
         * where MSG_NIGHT_WAIT = "NIGHT_WAIT Waiting for the werewolf"
         */
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
     * Sent: in start_voting_phase() to DEAD players (not in protocol.h!).
     * server.c line: snprintf(msg,...,"%s Players are voting\n",MSG_WAIT_VOTE)
     * We defined MSG_WAIT_VOTE above since it's missing from protocol.h.
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
     * server.c then calls start_statement_phase() again — so we will
     * receive YOUR_STATEMENT or WAIT_STATEMENT next, which update state.
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
     *
     * CRITICAL: do NOT set STATE_DONE here — the server will send
     * WELCOME right after, and we must be alive to receive it and
     * re-enter the lobby for the next game.
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
        /* State will be reset to STATE_LOBBY when WELCOME arrives next. */

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
        /* State will be reset to STATE_LOBBY when WELCOME arrives next. */

    /* ==================================================================
     * ERROR <reason>
     * Sent: private, for any invalid command the server rejects.
     * Re-show the prompt so the player knows they can try again.
     * ================================================================== */
    } else if (STARTS(MSG_ERROR)) {
        printf("[Error] %s\n", ARG(MSG_ERROR));
        /* Re-show the appropriate prompt for the current state */
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

/* -------------------------------------------------------------------------
 * main — no arguments needed; always connects to localhost:DEFAULT_PORT.
 * ---------------------------------------------------------------------- */
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

    /* ------------------------------------------------------------------
     * Step 1: Create socket  (lecture Image 2 style)
     * ------------------------------------------------------------------ */
    int soc = socket(AF_INET, SOCK_STREAM, 0);
    if (soc == -1) {
        perror("socket");
        exit(1);
    }

    /* ------------------------------------------------------------------
     * Step 2: Resolve host with getaddrinfo(), fill sockaddr_in
     *         (lecture Image 1 style)
     * ------------------------------------------------------------------ */
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

    if (connect(soc, result->ai_addr, result->ai_addrlen) == -1) {
        perror("connect");
        exit(1);
    }

    freeaddrinfo(result);

    printf("Connected to %s:%d\n", host, port);

    /* ------------------------------------------------------------------
     * Step 4: select() event loop  (lecture Image 3 style)
     *
     * Two fds:
     *   soc          — server messages, newline-delimited
     *   STDIN_FILENO — keyboard input from the player
     *
     * We run forever (until the server closes the socket) because
     * the server can reset the game and send WELCOME again after
     * GAME_OVER or GAME_ABORTED.
     * ------------------------------------------------------------------ */
    LineBuf sock_buf, stdin_buf;
    memset(&sock_buf,  0, sizeof(sock_buf));
    memset(&stdin_buf, 0, sizeof(stdin_buf));

    char tmp[BUF_SIZE];        /* raw bytes from read()         */
    char line[MAX_LINE_LEN];   /* one extracted protocol line   */

    while (1) {   /* loop forever; exit only when server closes connection */

        /* Build the fd_set — Image 3 style */
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

        /* ---- Data from server ---------------------------------------- */
        if (FD_ISSET(soc, &read_fds)) {
            int r = read(soc, tmp, sizeof(tmp) - 1);
            if (r < 0) {
                perror("read");
                break;
            } else if (r == 0) {
                /* Server closed the connection — truly done */
                printf("[Connection] Server disconnected.\n");
                break;
            } else {
                if (buf_append(&sock_buf, tmp, r) < 0) {
                    fprintf(stderr, "[Error] Server line too long, resetting buffer.\n");
                    sock_buf.len = 0;
                }
                /* Drain all complete lines */
                while (buf_next_line(&sock_buf, line, sizeof(line))) {
                    handle_server_line(line);
                }
            }
        }

        /* ---- Input from stdin ---------------------------------------- */
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            int r = read(STDIN_FILENO, tmp, sizeof(tmp) - 1);
            if (r < 0) {
                perror("read");
                break;
            } else if (r == 0) {
                /* EOF (Ctrl-D) — send QUIT and exit */
                write(soc, CMD_QUIT "\n", strlen(CMD_QUIT) + 1);
                break;
            } else {
                if (buf_append(&stdin_buf, tmp, r) < 0) {
                    fprintf(stderr, "[Error] Input line too long, discarding.\n");
                    stdin_buf.len = 0;
                }
                /* Forward each complete stdin line to the server */
                while (buf_next_line(&stdin_buf, line, sizeof(line))) {
                    if (line[0] == '\0') continue;  /* skip blank lines */
                    
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