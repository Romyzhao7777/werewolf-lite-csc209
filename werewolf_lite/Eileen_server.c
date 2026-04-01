#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include "protocol.h"
#include "game.h"

//注意最后改成protocal的
#define PORT DEFAULT_PORT
#define MAX_CLIENTS MAX_PLAYERS
#define MAX_NAME_LEN 64

typedef struct {
    int fd;
    int active;
    int has_name;
    char name[MAX_NAME_LEN];
} Client;


// Helper functions
int find_client_index_by_fd(Client clients[], int fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].fd == fd) {
            return i;
        }
    }
    return -1;
}

int count_named_clients(Client clients[]) {
    int count = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].has_name) {
            count++;
        }
    }
    return count;
}

int is_name_taken(Client clients[], const char *name) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].has_name &&
            strcmp(clients[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

void broadcast(Client clients[], const char *msg) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            write(clients[i].fd, msg, strlen(msg));
        }
    }
}

void send_roles_from_game(Client clients[], GameState *game) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].has_name) {
            char msg[MAX_LINE_LEN];
            if (game->players[i].role == ROLE_WEREWOLF) {
                snprintf(msg, sizeof(msg), "%s %s\n", MSG_ROLE, ROLE_STR_WEREWOLF);
            } else if (game->players[i].role == ROLE_VILLAGER) {
                snprintf(msg, sizeof(msg), "%s %s\n", MSG_ROLE, ROLE_STR_VILLAGER);
            } else {
                continue;
            }
            write(clients[i].fd, msg, strlen(msg));
        }
    }
}

void send_to_fd(int fd, const char *msg) {
    write(fd, msg, strlen(msg));
}

void start_night_phase(Client clients[], GameState *game) {
    broadcast(clients, MSG_NIGHT_START "\n");

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active || !clients[i].has_name) {
            continue;
        }

        char msg[MAX_LINE_LEN];

        if (game->players[i].role == ROLE_WEREWOLF) {
            snprintf(msg, sizeof(msg), "%s\n", MSG_NIGHT_ACTION);
        } else if (game->players[i].role == ROLE_VILLAGER) {
            snprintf(msg, sizeof(msg), "%s\n", MSG_NIGHT_WAIT);
        } else {
            continue;
        }

        send_to_fd(clients[i].fd, msg);
    }
}

void start_day_announce_phase(Client clients[], GameState *game) {
    broadcast(clients, MSG_DAY_START "\n");

    if (game->night_victim_slot >= 0 &&
        game->night_victim_slot < MAX_PLAYERS &&
        game->players[game->night_victim_slot].slot_used) {

        char elim_msg[MAX_LINE_LEN];
        snprintf(elim_msg, sizeof(elim_msg), "%s %s\n",
                 MSG_PLAYER_ELIMINATED,
                 game->players[game->night_victim_slot].name);
        broadcast(clients, elim_msg);
    }

    char alive_buf[MAX_LINE_LEN];
    if (game_format_alive_players(game, alive_buf, sizeof(alive_buf)) >= 0) {
        char alive_msg[MAX_LINE_LEN * 2];
        snprintf(alive_msg, sizeof(alive_msg), "%s %s\n",
                 MSG_ALIVE_PLAYERS, alive_buf);
        broadcast(clients, alive_msg);
    }
}

int find_next_speaker(GameState *game) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (game->players[i].slot_used &&
            game->players[i].has_name &&
            game->players[i].alive &&
            !game->players[i].has_spoken) {
            return i;
        }
    }
    return -1;
}
//注意考虑书否允许死者听发言
void prompt_statement_turn(Client clients[], GameState *game, int speaker_idx) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active || !clients[i].has_name) {
            continue;
        }

        char msg[MAX_LINE_LEN];

        if (i == speaker_idx) {
            snprintf(msg, sizeof(msg), "%s\n", MSG_YOUR_STATEMENT);
        } else {
            snprintf(msg, sizeof(msg), "%s %s is speaking\n",
                     MSG_WAIT_STATEMENT, game->players[speaker_idx].name);
        }

        write(clients[i].fd, msg, strlen(msg));
    }
}

void start_statement_phase(Client clients[], GameState *game) {
    game->phase = PHASE_STATEMENT;

    int speaker_idx = find_next_speaker(game);
    game->statement_turn = speaker_idx;

    if (speaker_idx == -1) {
        broadcast(clients, MSG_STATEMENT_PHASE_END "\n");
        return;
    }

    prompt_statement_turn(clients, game, speaker_idx);
}

int main() {
    //初始化随机数生成器
    srand((unsigned int)getpid());

    //初始化游戏状态
    GameState game;
    game_init(&game);

    //定义client结构体数组，存储客户端信息
    Client clients[MAX_CLIENTS];

    int server_fd, client_fd;
    struct sockaddr_in server_addr;

    // 1. 创建 socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(1);
    }

    // 2. 配置地址
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // 3. bind
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(1);
    }

    // 4. listen
    if (listen(server_fd, 5) < 0) {
        perror("listen");
        exit(1);
    }

    printf("Server listening on port %d...\n", PORT);


    fd_set master_set, read_fds;
    int max_fd;

    // 初始化
    FD_ZERO(&master_set);
    FD_SET(server_fd, &master_set);
    max_fd = server_fd;

    //初始化client数组
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
        clients[i].active = 0;
        clients[i].has_name = 0;
        clients[i].name[0] = '\0';
    }


    while (1) {
    read_fds = master_set;  // 拷贝

    if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
        perror("select");
        exit(1);
    }

    // 遍历所有 fd，处理新加入的以及有新传输内容的client
    for (int i = 0; i <= max_fd; i++) {
        if (FD_ISSET(i, &read_fds)) {

            // 🟢 新连接
            if (i == server_fd) {
                client_fd = accept(server_fd, NULL, NULL);
                if (client_fd < 0) {
                    perror("accept");
                    continue;
                }

                printf("New client connected!\n");
                
                
                // 检测是否有空位并添加新 client在 clients 数组中
                int added = 0;
                for (int j = 0; j < MAX_CLIENTS; j++) {
                    if (!clients[j].active) {
                        clients[j].fd = client_fd;
                        clients[j].active = 1;
                        added = 1;
                        break;
                    }
                }

                if (!added) {
                    printf("Server full\n");
                    close(client_fd);
                    continue;
                }

                printf("Current clients: ");
                for (int j = 0; j < MAX_CLIENTS; j++) {
                    if (clients[j].active) {
                        printf("%d ", clients[j].fd);
                    }
                }
                printf("\n");


                FD_SET(client_fd, &master_set);
                if (client_fd > max_fd) max_fd = client_fd;

                char msg[MAX_LINE_LEN];
                snprintf(msg, sizeof(msg), "%s\n", MSG_WELCOME);
                write(client_fd, msg, strlen(msg));
            }
            // 🔵 已有 client 发消息
            else {
    char buffer[MAX_LINE_LEN];
    int n = read(i, buffer, sizeof(buffer) - 1);

    if (n <= 0) {
        printf("Client disconnected (fd=%d)\n", i);

        close(i);
        FD_CLR(i, &master_set);

        char disconnected_name[MAX_NAME_LEN] = "";
        int disconnected_idx = -1;

        for (int j = 0; j < MAX_CLIENTS; j++) {
            if (clients[j].active && clients[j].fd == i) {
                disconnected_idx = j;
                if (clients[j].has_name) {
                    strcpy(disconnected_name, clients[j].name);
                }
                clients[j].active = 0;
                clients[j].fd = -1;
                clients[j].has_name = 0;
                clients[j].name[0] = '\0';

                game.players[j].fd = -1;
                game.players[j].name[0] = '\0';
                game.players[j].role = ROLE_NONE;
                game.players[j].slot_used = false;
                game.players[j].has_name = false;
                game.players[j].alive = false;
                game.players[j].has_spoken = false;
                game.players[j].has_voted = false;
                game.players[j].vote_target[0] = '\0';
                break;
            }
        }

        if (disconnected_idx != -1 && game.phase != PHASE_LOBBY) {
            char msg[MAX_LINE_LEN];

            if (disconnected_name[0] != '\0') {
                snprintf(msg, sizeof(msg), "%s %s\n", MSG_PLAYER_DISCONNECTED, disconnected_name);
                broadcast(clients, msg);
            }

            broadcast(clients, MSG_GAME_ABORTED "\n");

            game_init(&game);

            for (int k = 0; k < MAX_CLIENTS; k++) {
                if (clients[k].active) {
                    clients[k].has_name = 0;
                    clients[k].name[0] = '\0';

                    char welcome_msg[MAX_LINE_LEN];
                    snprintf(welcome_msg, sizeof(welcome_msg), "%s\n", MSG_WELCOME);
                    write(clients[k].fd, welcome_msg, strlen(welcome_msg));
                }
            }
        }
    } else {
        buffer[n] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';

        int idx = find_client_index_by_fd(clients, i);
        if (idx == -1) {
            continue;
        }

        printf("Received from fd=%d: %s\n", i, buffer);

        //把用户输入变成命令和参数，cmd是命令，arg是参数
        char cmd[MAX_LINE_LEN] = {0};
        char arg[MAX_LINE_LEN] = {0};
        int parts = sscanf(buffer, "%s %[^\n]", cmd, arg);
        if (parts < 2) {
            arg[0] = '\0';
        }
        //如果用户没注册名字
        if (!clients[idx].has_name) {
            //如果命令不是NAME
            if (strcmp(cmd, CMD_NAME) != 0) {
                char msg[MAX_LINE_LEN];
                snprintf(msg, sizeof(msg), "%s %s\n", MSG_ERROR, ERR_INVALID_COMMAND);
                write(i, msg, strlen(msg));
                continue;
            }
            //验证名字合法性
            if (strlen(arg) == 0 || strlen(arg) >= MAX_NAME_LEN) {
                char msg[MAX_LINE_LEN];
                snprintf(msg, sizeof(msg), "%s %s\n", MSG_ERROR, ERR_INVALID_NAME);
                write(i, msg, strlen(msg));
                continue;
            }
            //验证名字是否重复
            if (is_name_taken(clients, arg)) {
                char msg[MAX_LINE_LEN];
                snprintf(msg, sizeof(msg), "%s %s\n", MSG_ERROR, ERR_NAME_TAKEN);
                write(i, msg, strlen(msg));
                continue;
            }
            //注册名字
            strcpy(clients[idx].name, arg);
            clients[idx].has_name = 1;

            //将用户名同步给game
            game.players[idx].slot_used = true;
            game.players[idx].has_name = true;
            strcpy(game.players[idx].name, arg);
            game.players[idx].fd = i;

            printf("Registered player: %s (fd=%d)\n", clients[idx].name, i);

            //计算当前已注册玩家数量，并广播等待消息
            int named_count = count_named_clients(clients);

            //如果已注册玩家数量达到MAX_PLAYERS，广播游戏开始消息，分配角色并发送角色信息
            if (named_count == MAX_PLAYERS) {
                broadcast(clients, MSG_GAME_START "\n");

                game_assign_roles(&game);
                game.phase = PHASE_NIGHT;
                send_roles_from_game(clients, &game);
                start_night_phase(clients, &game);
            }

            else{
                char waiting_msg[MAX_LINE_LEN];
                snprintf(waiting_msg, sizeof(waiting_msg), "%s %d/%d players\n",
                     MSG_WAITING, named_count, MAX_PLAYERS);
                broadcast(clients, waiting_msg);
            }
        } 
        //用户已有name，处理游戏内命令，以及无效命令
        else {
            if (game.phase == PHASE_NIGHT) {
                if (strcmp(cmd, CMD_KILL) != 0) {
                    char msg[MAX_LINE_LEN];
                    snprintf(msg, sizeof(msg), "%s %s\n", MSG_ERROR, ERR_INVALID_COMMAND);
                    write(i, msg, strlen(msg));
                    continue;
                }

                if (game.players[idx].role != ROLE_WEREWOLF) {
                    char msg[MAX_LINE_LEN];
                    snprintf(msg, sizeof(msg), "%s %s\n", MSG_ERROR, ERR_INVALID_COMMAND);
                    write(i, msg, strlen(msg));
                    continue;
                }

                if (!game_valid_night_target(&game, idx, arg)) {
                    char msg[MAX_LINE_LEN];
                    snprintf(msg, sizeof(msg), "%s %s\n", MSG_ERROR, ERR_INVALID_KILL_TARGET);
                    write(i, msg, strlen(msg));
                    continue;
                }

                int victim_idx = game_find_player_by_name(&game, arg);
                if (victim_idx < 0) {
                    char msg[MAX_LINE_LEN];
                    snprintf(msg, sizeof(msg), "%s %s\n", MSG_ERROR, ERR_INVALID_KILL_TARGET);
                    write(i, msg, strlen(msg));
                    continue;
                }

                game.night_victim_slot = victim_idx;

                printf("Werewolf selected victim: %s (slot=%d)\n",
                    game.players[victim_idx].name, victim_idx);

                //夜里杀人后直接进入白天宣布阶段，宣布死者和存活玩家
                game.players[victim_idx].alive = false;
                game.phase = PHASE_DAY_ANNOUNCE;
                start_day_announce_phase(clients, &game);

                // 先把“公布死亡”做完，再清 round flags
                game_reset_round_flags(&game);
                //宣布阶段结束后进入发言阶段，按玩家顺序提示发言
                start_statement_phase(clients, &game);
            }
            else if (game.phase == PHASE_STATEMENT) {
                if (!game.players[idx].alive) {
                    char msg[MAX_LINE_LEN];
                    snprintf(msg, sizeof(msg), "%s You are dead and cannot speak\n", MSG_ERROR);
                    write(i, msg, strlen(msg));
                    continue;
                }

                if (idx != game.statement_turn) {
                    char msg[MAX_LINE_LEN];
                    snprintf(msg, sizeof(msg), "%s %s\n", MSG_ERROR, ERR_NOT_YOUR_TURN);
                    write(i, msg, strlen(msg));
                    continue;
                }

                if (strcmp(cmd, CMD_SAY) != 0) {
                    char msg[MAX_LINE_LEN];
                    snprintf(msg, sizeof(msg), "%s %s\n", MSG_ERROR, ERR_INVALID_COMMAND);
                    write(i, msg, strlen(msg));
                    continue;
                }

                if (strlen(arg) == 0 || strlen(arg) > MAX_STATEMENT_LEN) {
                    char msg[MAX_LINE_LEN];
                    snprintf(msg, sizeof(msg), "%s %s\n", MSG_ERROR, ERR_INVALID_COMMAND);
                    write(i, msg, strlen(msg));
                    continue;
                }

                char statement_msg[MAX_LINE_LEN * 2];
                snprintf(statement_msg, sizeof(statement_msg), "%s %s: %s\n",
                        MSG_STATEMENT, game.players[idx].name, arg);
                broadcast(clients, statement_msg);

                game.players[idx].has_spoken = true;

                int next_idx = find_next_speaker(&game);
                game.statement_turn = next_idx;

                if (next_idx == -1) {
                    broadcast(clients, MSG_STATEMENT_PHASE_END "\n");
                    game.phase = PHASE_VOTING;
                } else {
                    prompt_statement_turn(clients, &game, next_idx);
                }
            }
             else {
                char msg[MAX_LINE_LEN];
                snprintf(msg, sizeof(msg), "%s %s\n", MSG_ERROR, ERR_NOT_IMPLEMENTED);
                write(i, msg, strlen(msg));
            }
        }
    }
}
        }
    }
}

    //close(client_fd);
    //close(server_fd);
    return 0;
}