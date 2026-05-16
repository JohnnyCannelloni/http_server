#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>

#define PORT 8080

int main(void) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(1); }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(server_fd, 16) < 0) { perror("listen"); exit(1); } 

    printf("Listening on port %d. Press Ctrl-C to quit.\n", PORT);

    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) { perror("accept"); continue; }

        printf("Got a connection from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        char buf[4096];
        ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
        if (n <= 0) {
            close(client_fd);
            continue;
        }
        buf[n] = '\0';

        char method[16];
        char path[1024];
        char version[16];

        if (sscanf(buf, "%15s %1023s %15s", method, path, version) != 3) {
            printf("Malformed request\n");
            close(client_fd);
            continue;
        }

        printf("Method: %s\n", method);
        printf("Path: %s\n", path);
        printf("Version: %s\n", version);

        const char *body = 
            "<html><body><h1>Hello, World!</h1></body></html>";

        char response[1024];
        int response_len = snprintf(response, sizeof(response), 
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            strlen(body), body);
        
        write(client_fd, response, response_len);
        close(client_fd);
    }

    close(server_fd);

    return 0;
} 