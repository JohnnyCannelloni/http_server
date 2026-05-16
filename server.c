#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <fcntl.h>

#define PORT 8080
#define WEB_ROOT "./www"

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

        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s%s", WEB_ROOT, path);

        int file_fd = open(fullpath, O_RDONLY);
        if (file_fd < 0) {
            const char *not_found =
                "HTTP/1.0 404 Not Found\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 13\r\n"
                "Connection: close\r\n"
                "\r\n"
                "404 Not Found";
            write(client_fd, not_found, strlen(not_found));
            close(client_fd);
            continue;
        }

        struct stat st;
        if (fstat(file_fd, &st) < 0) {
            perror("fstat");
            close(file_fd);
            close(client_fd);
            continue;
        }

        char header[512];
        int header_len = snprintf(header, sizeof(header),
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %lld\r\n"
            "Connection: close\r\n"
            "\r\n", 
            (long long)st.st_size);
        
        write(client_fd, header, header_len);

        char chunk[4096];
        while ((n = read(file_fd, chunk, sizeof(chunk))) > 0) {
            write(client_fd, chunk, n);
        }

        close(file_fd);
        close(client_fd);
    }

    close(server_fd);

    return 0;
} 