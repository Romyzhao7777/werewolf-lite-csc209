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

/* Reset all fields to lobby defaults */
void game_init(GameState *g);

/*
 * Pick one random werewolf among registered players; others become villagers; all set alive.
 */
void game_assign_roles(GameState *g);

/*
 * Number of players with slot_used && has_name && alive.
 */
int game_alive_count(const GameState *g);

/*
 * Write space-separated alive player names into buf (for ALIVE_PLAYERS). Returns bytes written, or -1.
 */
int game_format_alive_players(const GameState *g, char *buf, size_t buflen);

/*
 * Return slot index for exact name match, or -1.
 */
int game_find_player_by_name(const GameState *g, const char *name);

/*
 * True if caller is the wolf slot and target is another alive player by name.
 */
bool game_valid_night_target(const GameState *g, int werewolf_slot, const char *target_name);

/*
 * True if voter may cast this vote (alive, not yet voted) and target is alive.
 */
bool game_valid_vote_target(const GameState *g, int voter_slot, const char *target_name);

/*
 * Tally has_voted + vote_target; *elim_slot = sole top vote-getter, or -1 on tie / no votes.
 */
void game_tally_votes(GameState *g, int *elim_slot);

/* True if the werewolf player is eliminated */
bool game_villagers_win(const GameState *g);

/* True if alive wolves >= alive villagers (and at least one wolf alive) */
bool game_werewolf_win(const GameState *g);

/*
 * Clear night victim index, vote/speech flags, statement_turn, votes_received.
 */
void game_reset_round_flags(GameState *g);

#endif /* GAME_H */
