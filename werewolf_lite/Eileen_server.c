#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>


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

    char buffer[512];

    // 外层循环：不断接受新 client
    while (1) {

        // 5. accept 一个 client
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        printf("Client connected!\n");

        // 6. 发送 WELCOME
        char *msg = "WELCOME\n";
        write(client_fd, msg, strlen(msg));

        // 内层循环：处理这个 client
        while (1) {
            int n = read(client_fd, buffer, sizeof(buffer) - 1);

            if (n <= 0) {
                printf("Client disconnected\n");
                close(client_fd);
                break;  // 只跳出内层循环
            }

            buffer[n] = '\0';
            printf("Received: %s", buffer);
        }
    }

    close(client_fd);
    close(server_fd);
    return 0;
}