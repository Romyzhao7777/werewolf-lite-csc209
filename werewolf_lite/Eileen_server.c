#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include "protocol.h"

//注意最后改成protocal的
#define PORT DEFAULT_PORT
#define MAX_CLIENTS MAX_PLAYERS
#define MAX_NAME_LEN 64

typedef struct {
    int fd;
    int active;
    int has_name;
    char name[MAX_NAME_LEN];
    int role;   // 0 = villager, 1 = werewolf
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

void assign_roles(Client clients[]) {
    int werewolf_index = rand() % MAX_PLAYERS;

    int named_seen = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].has_name) {
            if (named_seen == werewolf_index) {
                clients[i].role = 1; // werewolf
            } else {
                clients[i].role = 0; // villager
            }
            named_seen++;
        }
    }
}

void send_roles(Client clients[]) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].has_name) {
            char msg[MAX_LINE_LEN];
            if (clients[i].role == 1) {
                snprintf(msg, sizeof(msg), "%s %s\n", MSG_ROLE, ROLE_STR_WEREWOLF);
            } else {
                snprintf(msg, sizeof(msg), "%s %s\n", MSG_ROLE, ROLE_STR_VILLAGER);
            }
            write(clients[i].fd, msg, strlen(msg));
        }
    }
}

int main() {
    //初始化随机数生成器
    srand((unsigned int)getpid());

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
        clients[i].role = 0;
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

                char *msg = "WELCOME\n";
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

        for (int j = 0; j < MAX_CLIENTS; j++) {
            if (clients[j].active && clients[j].fd == i) {
                clients[j].active = 0;
                clients[j].fd = -1;
                clients[j].has_name = 0;
                clients[j].name[0] = '\0';
                clients[j].role = 0;
                break;
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
        sscanf(buffer, "%s %[^\n]", cmd, arg);

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

            printf("Registered player: %s (fd=%d)\n", clients[idx].name, i);

            //计算当前已注册玩家数量，并广播等待消息
            int named_count = count_named_clients(clients);

            //如果已注册玩家数量达到MAX_PLAYERS，广播游戏开始消息，分配角色并发送角色信息
            if (named_count == MAX_PLAYERS) {
                broadcast(clients, MSG_GAME_START "\n");

                assign_roles(clients);
                send_roles(clients);
            }

            else{
                char waiting_msg[MAX_LINE_LEN];
                snprintf(waiting_msg, sizeof(waiting_msg), "%s %d/%d players\n",
                     MSG_WAITING, named_count, MAX_PLAYERS);
                broadcast(clients, waiting_msg);
            }
        } 
        //如果用户已经注册名字，但重复注册
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

    //close(client_fd);
    //close(server_fd);
    return 0;
}