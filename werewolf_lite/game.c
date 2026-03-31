/*
 * Werewolf Lite — pure game rules on GameState (no I/O).
 * Implementation matches declarations in game.h (all APIs documented there).
 */

#include "game.h"

#include <stdlib.h>
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

/* Randomly assign 1 werewolf + 3 villagers among registered players*/
void game_assign_roles(GameState *g) {
    int in_game[MAX_PLAYERS];
    int n = 0;

    /* Collect slots that have joined with a valid name / 收集已起名的槽位 */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (g->players[i].slot_used && g->players[i].has_name && g->players[i].name[0] != '\0') {
            in_game[n++] = i;
        }
    }

    if (n < MIN_PLAYERS) {
        g->werewolf_slot = -1;
        return;
    }

    /* RNG seed: srand() once in server main / 随机种子在 server 的 main 里调用一次 */
    int wolf_idx = rand() % n;
    g->werewolf_slot = in_game[wolf_idx];

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!g->players[i].slot_used || !g->players[i].has_name || g->players[i].name[0] == '\0') {
            continue;
        }
        if (i == g->werewolf_slot) {
            g->players[i].role = ROLE_WEREWOLF;
        } else {
            g->players[i].role = ROLE_VILLAGER;
        }
        g->players[i].alive = true;
    }
}

/* Count alive players who are in the game */
int game_alive_count(const GameState *g) {
    int c = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (g->players[i].slot_used && g->players[i].has_name && g->players[i].alive) {
            c++;
        }
    }
    return c;
}

/*
 * Build space-separated alive names into buf for ALIVE_PLAYERS line.
 * 拼 ALIVE_PLAYERS 所需字符串。
 * Returns bytes written (excluding null terminator), or -1 on error / 返回写入字节数（不含结尾 0），失败 -1。
 */
int game_format_alive_players(const GameState *g, char *buf, size_t buflen) {
    if (!buf || buflen == 0) {
        return -1;
    }
    buf[0] = '\0';
    size_t pos = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!g->players[i].slot_used || !g->players[i].has_name || !g->players[i].alive) {
            continue;
        }
        const char *nm = g->players[i].name;
        size_t len = strlen(nm);
        size_t need = len + (pos > 0 ? 1u : 0u);
        if (pos + need + 1 > buflen) {
            return -1;
        }
        if (pos > 0) {
            buf[pos++] = ' ';
        }
        memcpy(buf + pos, nm, len);
        pos += len;
        buf[pos] = '\0';
    }
    return (int)pos;
}

/* Linear search by exact name match / 按名线性查找 */
int game_find_player_by_name(const GameState *g, const char *name) {
    if (!name || name[0] == '\0') {
        return -1;
    }
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (g->players[i].slot_used && g->players[i].has_name &&
            strcmp(g->players[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/*
 * Night kill target must be another alive player (not the wolf).
 * 夜里目标须为另一名存活玩家（不能自刀）。
 */
bool game_valid_night_target(const GameState *g, int werewolf_slot, const char *target_name) {
    if (!g || !target_name || target_name[0] == '\0') {
        return false;
    }
    if (werewolf_slot < 0 || werewolf_slot >= MAX_PLAYERS || werewolf_slot != g->werewolf_slot) {
        return false;
    }
    int t = game_find_player_by_name(g, target_name);
    if (t < 0) {
        return false;
    }
    if (!g->players[t].alive || t == werewolf_slot) {
        return false;
    }
    return true;
}

/*
 * Voter must be alive and not have voted yet; target must be an alive player.
 * 投票者须存活且未投过票；目标须存活。
 */
bool game_valid_vote_target(const GameState *g, int voter_slot, const char *target_name) {
    if (!g || !target_name || target_name[0] == '\0') {
        return false;
    }
    if (voter_slot < 0 || voter_slot >= MAX_PLAYERS) {
        return false;
    }
    if (!g->players[voter_slot].slot_used || !g->players[voter_slot].has_name ||
        !g->players[voter_slot].alive || g->players[voter_slot].has_voted) {
        return false;
    }
    int t = game_find_player_by_name(g, target_name);
    if (t < 0 || !g->players[t].alive) {
        return false;
    }
    return true;
}

/*
 * Count votes per target; *elim_slot = sole top vote, or -1 on tie / no votes / error.
 */
void game_tally_votes(GameState *g, int *elim_slot) {
    int counts[MAX_PLAYERS];

    if (elim_slot) {
        *elim_slot = -1;
    }
    if (!g) {
        return;
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        counts[i] = 0;
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!g->players[i].slot_used || !g->players[i].has_name || !g->players[i].alive) {
            continue;
        }
        if (!g->players[i].has_voted || g->players[i].vote_target[0] == '\0') {
            continue;
        }
        int t = game_find_player_by_name(g, g->players[i].vote_target);
        if (t < 0 || t >= MAX_PLAYERS) {
            continue;
        }
        counts[t]++;
    }

    int max_votes = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (counts[i] > max_votes) {
            max_votes = counts[i];
        }
    }
    if (max_votes <= 0) {
        if (elim_slot) {
            *elim_slot = -1;
        }
        return;
    }

    int tie_slots = 0;
    int sole = -1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (counts[i] == max_votes) {
            tie_slots++;
            sole = i;
        }
    }
    if (tie_slots != 1) {
        if (elim_slot) {
            *elim_slot = -1;
        }
        return;
    }

    if (elim_slot) {
        *elim_slot = sole;
    }
}

/* Villagers win when the werewolf is dead / 狼人死亡则村民胜 */
bool game_villagers_win(const GameState *g) {
    if (!g) {
        return false;
    }
    if (g->werewolf_slot < 0 || g->werewolf_slot >= MAX_PLAYERS) {
        return false;
    }
    return !g->players[g->werewolf_slot].alive;
}

/*
 * Wolf side wins when alive wolves >= alive villagers (and at least one wolf alive).
 * 存活狼数不少于存活民数且仍有狼存活时狼人阵营胜。
 */
bool game_werewolf_win(const GameState *g) {
    if (!g) {
        return false;
    }
    int wolves = 0;
    int villagers = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!g->players[i].slot_used || !g->players[i].has_name || !g->players[i].alive) {
            continue;
        }
        if (g->players[i].role == ROLE_WEREWOLF) {
            wolves++;
        } else if (g->players[i].role == ROLE_VILLAGER) {
            villagers++;
        }
    }
    return wolves > 0 && wolves >= villagers;
}

/*
 * Clear per-round / per-phase scratch flags before the next night or vote cycle.
 * 新一轮前清空夜间受害者、投票与发言状态等。
 */
void game_reset_round_flags(GameState *g) {
    if (!g) {
        return;
    }
    g->night_victim_slot = -1;
    g->votes_received = 0;
    g->statement_turn = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        g->players[i].has_spoken = false;
        g->players[i].has_voted = false;
        g->players[i].vote_target[0] = '\0';
    }
}
