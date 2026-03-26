#ifndef GAME_H
#define GAME_H

#include "protocol.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    PHASE_LOBBY,
    PHASE_ROLE_ASSIGN,
    PHASE_NIGHT,
    PHASE_DAY_ANNOUNCE,
    PHASE_STATEMENT,
    PHASE_VOTING,
    PHASE_WIN_CHECK,
    PHASE_GAME_OVER
} GamePhase;

typedef enum { ROLE_NONE = 0, ROLE_WEREWOLF, ROLE_VILLAGER } Role;

typedef struct {
    int fd;
    char name[MAX_NAME_LEN];
    Role role;
    bool slot_used;
    bool has_name;
    bool alive;
    bool has_spoken;
    bool has_voted;
    char vote_target[MAX_NAME_LEN];
} Player;

typedef struct {
    Player players[MAX_PLAYERS];
    GamePhase phase;
    int round;
    int werewolf_slot;
    int night_victim_slot;
    int statement_turn;
    int votes_received;
} GameState;

void game_init(GameState *g);

/* TODO: assign 1 werewolf and 3 villagers randomly */
void game_assign_roles(GameState *g);

/* TODO: count alive players with valid names */
int game_alive_count(const GameState *g);

/* TODO: write space-separated alive names into buf */
int game_format_alive_players(const GameState *g, char *buf, size_t buflen);

/* TODO: return player index or -1 */
int game_find_player_by_name(const GameState *g, const char *name);

/* TODO: validate night kill target */
bool game_valid_night_target(const GameState *g, int werewolf_slot, const char *target_name);

/* TODO: validate vote target */
bool game_valid_vote_target(const GameState *g, int voter_slot, const char *target_name);

/* TODO: tally votes; *elim_slot = -1 on tie or error */
void game_tally_votes(GameState *g, int *elim_slot);

bool game_villagers_win(const GameState *g);
bool game_werewolf_win(const GameState *g);

/* TODO: reset per-round flags (night victim index, votes, etc.) */
void game_reset_round_flags(GameState *g);

#endif /* GAME_H */
