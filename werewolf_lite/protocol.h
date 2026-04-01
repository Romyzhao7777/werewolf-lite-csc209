/*
 * Werewolf Lite — line-based text protocol (CSC209 A3, Category 2).
 *
 * Transport: TCP byte stream; logical messages are newline-delimited (\n).
 * Encoding: printable ASCII for keywords; player names and speech use the same line rules.
 *
 * Build: default listen/connect port matches Makefile PORT (see DEFAULT_PORT below).
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

/* Default TCP port; Makefile may pass -DDEFAULT_PORT=$(PORT). CSC209 default: 4242. */
#ifndef DEFAULT_PORT
#define DEFAULT_PORT 4242
#endif

#define MAX_PLAYERS 4
#define MIN_PLAYERS 4

#define MAX_NAME_LEN 64
#define MAX_STATEMENT_LEN 100
#define MAX_LINE_LEN 512

/*
 * --- Server -> client (keywords are first token; many lines add arguments after a space) ---
 *
 * WELCOME
 *   Sent once after TCP accept, before NAME is required.
 *
 * WAITING <n>/<MAX_PLAYERS> players
 *   Lobby progress; <n> is how many players have registered a name.
 *
 * GAME_START
 *   Broadcast when MIN_PLAYERS have joined; followed by private ROLE lines.
 *
 * ROLE WEREWOLF | ROLE VILLAGER
 *   Sent to one client only; must match ROLE_STR_*.
 *
 * NIGHT_START
 *   Broadcast; then NIGHT_ACTION to wolf, NIGHT_WAIT to villagers.
 *
 * NIGHT_ACTION Choose a player to eliminate   (full line; use MSG_NIGHT_ACTION)
 * NIGHT_WAIT Waiting for the werewolf        (full line; use MSG_NIGHT_WAIT)
 *
 * DAY_START
 * PLAYER_ELIMINATED <name>
 * ALIVE_PLAYERS <name1> <name2> ...   (space-separated, order implementation-defined)
 *
 * YOUR_STATEMENT
 *   Sent to current speaker only.
 * WAIT_STATEMENT <name> is speaking
 *   Sent to others while <name> may send SAY.
 *
 * STATEMENT <name>: <text>
 *   Broadcast; <text> may contain spaces (single line, MAX_STATEMENT_LEN for <text> recommended).
 *
 * STATEMENT_PHASE_END
 *
 * VOTE_PROMPT
 * VOTE_RESULT <name>          (who received most votes; see tie handling)
 * VOTE_TIE
 * NO_PLAYER_ELIMINATED
 *
 * GAME_OVER VILLAGERS_WIN | GAME_OVER WEREWOLF_WIN   (use WIN_STR_*)
 *
 * ERROR <reason>              (free-text <reason>; see ERR_* suggestions below)
 * PLAYER_DISCONNECTED <name>
 * GAME_ABORTED
 */
#define MSG_WELCOME "WELCOME"
#define MSG_WAITING "WAITING"
#define MSG_GAME_START "GAME_START"
#define MSG_ROLE "ROLE"
#define MSG_NIGHT_START "NIGHT_START"
#define MSG_NIGHT_ACTION "NIGHT_ACTION Choose a player to eliminate"
#define MSG_NIGHT_WAIT "NIGHT_WAIT Waiting for the werewolf"
#define MSG_DAY_START "DAY_START"
#define MSG_PLAYER_ELIMINATED "PLAYER_ELIMINATED"
#define MSG_ALIVE_PLAYERS "ALIVE_PLAYERS"
#define MSG_YOUR_STATEMENT "YOUR_STATEMENT"
#define MSG_WAIT_STATEMENT "WAIT_STATEMENT"
#define MSG_STATEMENT "STATEMENT"
#define MSG_STATEMENT_PHASE_END "STATEMENT_PHASE_END"
#define MSG_VOTE_PROMPT "VOTE_PROMPT"
#define MSG_VOTE_RESULT "VOTE_RESULT"
#define MSG_VOTE_TIE "VOTE_TIE"
#define MSG_NO_PLAYER_ELIMINATED "NO_PLAYER_ELIMINATED"
#define MSG_GAME_OVER "GAME_OVER"
#define MSG_ERROR "ERROR"
#define MSG_PLAYER_DISCONNECTED "PLAYER_DISCONNECTED"
#define MSG_GAME_ABORTED "GAME_ABORTED"
#define MSG_WAIT_VOTE "WAIT_VOTE"

/*
 * --- Client -> server (first token; one command per line) ---
 *
 * NAME <username>     — lobby; <username> non-empty, length < MAX_NAME_LEN
 * KILL <name>         — night; werewolf only
 * SAY <text>          — statement phase; current speaker only; <text> rest of line
 * VOTE <name>         — voting phase
 * QUIT                — optional; client may close socket instead
 */
#define CMD_NAME "NAME"
#define CMD_KILL "KILL"
#define CMD_SAY "SAY"
#define CMD_VOTE "VOTE"
#define CMD_QUIT "QUIT"

#define ROLE_STR_WEREWOLF "WEREWOLF"
#define ROLE_STR_VILLAGER "VILLAGER"

#define WIN_STR_VILLAGERS "VILLAGERS_WIN"
#define WIN_STR_WEREWOLF "WEREWOLF_WIN"

/*
 * Suggested ERROR <reason> payloads (optional; keep messages short, ASCII).
 */
#define ERR_INVALID_COMMAND "Invalid command"
#define ERR_INVALID_NAME "Invalid name"
#define ERR_NAME_TAKEN "Name already taken"
#define ERR_INVALID_KILL_TARGET "Invalid kill target"
#define ERR_INVALID_VOTE_TARGET "Invalid vote target"
#define ERR_NOT_YOUR_TURN "Not your turn to speak"
#define ERR_ALREADY_VOTED "Already voted"
#define ERR_LINE_TOO_LONG "Line too long"
#define ERR_NOT_IMPLEMENTED "Not implemented"

#endif /* PROTOCOL_H */
