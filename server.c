#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <string.h>
#include <fcntl.h>

#define PORT 8080
#define WEB_ROOT "./www"

static int setup_listening_socket(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(server_fd, 16) < 0) { perror("listen"); exit(1); } 

    return server_fd;
}

static int parse_request_line(const char *buf, char *method, char *path, char *version) {
    if (sscanf(buf, "%15s %1023s %15s", method, path, version) != 3) {
        return -1;
    }

    return 0;
}

static void send_error(int client_fd, int code, const char *reason) {
    char body[128];
    int body_len = snprintf(body, sizeof(body), "%d %s\n", code, reason);

    char response[512];
    int response_len = snprintf(response, sizeof(response),
        "HTTP/1.0 %d %s\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        code, reason, body_len, body);

    write(client_fd, response, response_len);
}

static void serve_file(int client_fd, const char *fullpath) {
    int file_fd = open(fullpath, O_RDONLY);
        if (file_fd < 0) {
            send_error(client_fd, 404, "Not Found");
            return;
        }

        struct stat st;
        if (fstat(file_fd, &st) < 0 || !S_ISREG(st.st_mode)) {
            send_error(client_fd, 404, "Not Found");
            close(file_fd);
            return;
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
        ssize_t n;
        while ((n = read(file_fd, chunk, sizeof(chunk))) > 0) {
            write(client_fd, chunk, n);
        }

        close(file_fd);
}

static void handle_client(int client_fd) {
        char buf[4096];
        ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
        if (n <= 0) {
            return;
        }
        buf[n] = '\0';

        char method[16], path[1024], version[16];
        if (parse_request_line(buf, method, path, version) != 0) {
            send_error(client_fd, 400, "Bad Request");
            return;
        }

        if (strcmp(method, "GET") != 0) {
            send_error(client_fd, 405, "Method Not Allowed");
            return;
        }

        char fullpath[1024];
        size_t plen = strlen(path);
        if (plen > 0 && path[plen - 1] == '/') {
            snprintf(fullpath, sizeof(fullpath), "%s%sindex.html", WEB_ROOT, path);
        } else {
            snprintf(fullpath, sizeof(fullpath), "%s%s", WEB_ROOT, path);
        }

        serve_file(client_fd, fullpath);
}

int main(void) {
    int server_fd = setup_listening_socket(PORT);
    printf("Listening on port %d. Press Ctrl-C to quit.\n", PORT);

    for (;;) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) { 
            perror("accept"); 
            continue; 
        }
        
        handle_client(client_fd);
        close(client_fd);
    }

    close(server_fd);

    return 0;
} 