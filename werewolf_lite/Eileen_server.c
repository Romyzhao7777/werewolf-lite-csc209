#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define PORT 4242

int main() {
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

                FD_SET(client_fd, &master_set);
                if (client_fd > max_fd) max_fd = client_fd;

                // 发 WELCOME
                char *msg = "WELCOME\n";
                write(client_fd, msg, strlen(msg));
            }

            // 🔵 已有 client 发消息
            else {
                char buffer[512];
                int n = read(i, buffer, sizeof(buffer) - 1);

                if (n <= 0) {
                    printf("Client disconnected\n");
                    close(i);
                    FD_CLR(i, &master_set);
                } else {
                    buffer[n] = '\0';
                    printf("Received from %d: %s", i, buffer);
                }
            }
        }
    }
}

    //close(client_fd);
    //close(server_fd);
    return 0;
}