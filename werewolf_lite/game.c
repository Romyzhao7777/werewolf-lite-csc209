#include "game.h"

#include <string.h>

void game_init(GameState *g) {
    memset(g, 0, sizeof(*g));
    g->phase = PHASE_LOBBY;
    g->round = 0;
    g->werewolf_slot = -1;
    g->night_victim_slot = -1;
    g->statement_turn = 0;
    g->votes_received = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        g->players[i].fd = -1;
        g->players[i].role = ROLE_NONE;
    }
}

void game_assign_roles(GameState *g) {
    (void)g;
    /* TODO: seed RNG if needed, pick werewolf_slot, set roles and alive flags */
}

int game_alive_count(const GameState *g) {
    (void)g;
    /* TODO */
    return 0;
}

int game_format_alive_players(const GameState *g, char *buf, size_t buflen) {
    (void)g;
    if (!buf || buflen == 0) {
        return -1;
    }
    buf[0] = '\0';
    /* TODO */
    return -1;
}

int game_find_player_by_name(const GameState *g, const char *name) {
    (void)g;
    (void)name;
    /* TODO */
    return -1;
}

bool game_valid_night_target(const GameState *g, int werewolf_slot, const char *target_name) {
    (void)g;
    (void)werewolf_slot;
    (void)target_name;
    /* TODO */
    return false;
}

bool game_valid_vote_target(const GameState *g, int voter_slot, const char *target_name) {
    (void)g;
    (void)voter_slot;
    (void)target_name;
    /* TODO */
    return false;
}

void game_tally_votes(GameState *g, int *elim_slot) {
    (void)g;
    if (elim_slot) {
        *elim_slot = -1;
    }
    /* TODO */
}

bool game_villagers_win(const GameState *g) {
    (void)g;
    /* TODO: true if werewolf is eliminated */
    return false;
}

bool game_werewolf_win(const GameState *g) {
    (void)g;
    /* TODO: true if werewolves >= villagers among alive */
    return false;
}

void game_reset_round_flags(GameState *g) {
    (void)g;
    /* TODO */
}
