#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <limits.h>
#include <getopt.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>
#include "queue.h"

#define MAX_WORKERS 256
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define KEEPALIVE_TIMEOUT_SECS 5    
#define MAX_REQUESTS_PER_CONN  100  

static const char *web_root = "./www";
static char web_root_abs[PATH_MAX]; 
static volatile sig_atomic_t stop_flag = 0;

struct log_entry {
    char client_ip[INET_ADDRSTRLEN];
    char request_line[256];
    int status;
    long bytes_sent;
};

static const struct {
    const char *ext;
    const char *mime;
} mime_table[] = {
    { ".html", "text/html" },
    { ".css",  "text/css" },
    { ".js",   "application/javascript" },
    { ".json", "application/json" },
    { ".txt",  "text/plain" },
    { ".png",  "image/png" },
    { ".jpg",  "image/jpeg" },
    { ".jpeg", "image/jpeg" },
    { ".gif",  "image/gif" },
    { ".svg",  "image/svg+xml" },
    { ".ico",  "image/x-icon" },
};

static void write_log(const struct log_entry *log);

static void on_signal(int sig) {
    (void)sig; // silence the warning
    stop_flag = 1;
}

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

    if (listen(server_fd, 128) < 0) { perror("listen"); exit(1); } 

    return server_fd;
}

static int parse_request_line(const char *buf, char *method, char *path, char *version) {
    if (sscanf(buf, "%15s %1023s %15s", method, path, version) != 3) {
        return -1;
    }

    return 0;
}

static void send_error(int client_fd, int code, const char *reason,
                        int keep_alive, struct log_entry *log) {
    char body[128];
    int body_len = snprintf(body, sizeof(body), "%d %s\n", code, reason);

    char response[512];
    int response_len = snprintf(response, sizeof(response),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %d\r\n"
        "Connection: %s\r\n"
        "\r\n"
        "%s",
        code, reason, body_len,
        keep_alive ? "keep-alive" : "close", body);

    write(client_fd, response, response_len);
    log->status = code;
    log->bytes_sent = body_len;
}

static const char *get_mime_type(const char *filepath) {
    const char *dot = strrchr(filepath, '.');

    if (dot == NULL) {
        return "application/octet-stream";
    }

    for (size_t i = 0; i < ARRAY_LEN(mime_table); i++) {
        if (strcasecmp(dot, mime_table[i].ext) == 0) {
            return mime_table[i].mime;
        }
    }

    return "application/octet-stream";
}

static void serve_file(int client_fd, const char *fullpath,
                        int keep_alive, struct log_entry *log) {
    char resolved[PATH_MAX];
    if (realpath(fullpath, resolved) == NULL) {
        send_error(client_fd, 404, "Not Found", keep_alive, log);
        return;
    }

    size_t root_len = strlen(web_root_abs);
    if (strncmp(resolved, web_root_abs, root_len) != 0 ||
       (resolved[root_len] != '/' && resolved[root_len] != '\0')) {
        send_error(client_fd, 403, "Forbidden", keep_alive, log);
        return;
    }

    int file_fd = open(fullpath, O_RDONLY);
        if (file_fd < 0) {
            send_error(client_fd, 404, "Not Found", keep_alive, log);
            return;
        }

        struct stat st;
        if (fstat(file_fd, &st) < 0 || !S_ISREG(st.st_mode)) {
            send_error(client_fd, 404, "Not Found", keep_alive, log);
            close(file_fd);
            return;
        }

        const char *mime = get_mime_type(fullpath);
        char header[512];
        int header_len = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %lld\r\n"
            "Connection: %s\r\n"
            "\r\n",
            mime, (long long)st.st_size,
            keep_alive ? "keep-alive" : "close");

        write(client_fd, header, header_len);

        char chunk[4096];
        ssize_t n;
        while ((n = read(file_fd, chunk, sizeof(chunk))) > 0) {
            write(client_fd, chunk, n);
        }

        log->status = 200;
        log->bytes_sent = st.st_size;
        close(file_fd);
}

// decide whether the connection should stay open after the request
// defaults to close unless stated otherwise
static int wants_keep_alive(const char *version, const char *headers) {
    int keep = (strcmp(version, "HTTP/1.1") == 0);
    const char *c = strcasestr(headers, "\nconnection:");
    if (c != NULL) {
        if (strcasestr(c, "close") != NULL) {
            keep = 0;
        } else if (strcasestr(c, "keep-alive") != NULL) {
            keep = 1;
        }
    }
    return keep;
}

// serve one request already sitting in the header 
// returns 1 if the connection may stay open, 0 otherwise
static int handle_one_request(int client_fd, const char *headers,
                              const char *client_ip) {
    struct log_entry log = {0};
    strncpy(log.client_ip, client_ip, sizeof(log.client_ip) - 1);

    char method[16], path[1024], version[16];
    if (parse_request_line(headers, method, path, version) != 0) {
        strcpy(log.request_line, "(malformed)");
        send_error(client_fd, 400, "Bad Request", 0, &log);
        write_log(&log);
        return 0;
    }

    snprintf(log.request_line, sizeof(log.request_line),
            "%s %s %s", method, path, version);

    int keep_alive = wants_keep_alive(version, headers);

    if (strcmp(method, "GET") != 0) {
        send_error(client_fd, 405, "Method Not Allowed", keep_alive, &log);
        write_log(&log);
        return keep_alive;
    }

    char fullpath[1024];
    size_t plen = strlen(path);
    if (plen > 0 && path[plen - 1] == '/') {
        snprintf(fullpath, sizeof(fullpath), "%s%sindex.html", web_root, path);
    } else {
        snprintf(fullpath, sizeof(fullpath), "%s%s", web_root, path);
    }

    serve_file(client_fd, fullpath, keep_alive, &log);
    write_log(&log);
    return keep_alive;
}

static void handle_client(int client_fd, const char *client_ip) {
    char buf[8192];
    size_t buf_len = 0;
    int req_count = 0;

    for (;;) {
        // accumulate bytes until we have a full header
        char *term;
        while ((term = memmem(buf, buf_len, "\r\n\r\n", 4)) == NULL) {
            if (buf_len == sizeof(buf)) break; // headers too large to buffer
            ssize_t n = read(client_fd, buf + buf_len, sizeof(buf) - buf_len);
            if (n <= 0) return; // EOF, idle timeout, or error -> close
            buf_len += (size_t)n;
        }

        int keep_alive;
        if (term == NULL) {
            // oversized headers
            struct log_entry log = {0};
            strncpy(log.client_ip, client_ip, sizeof(log.client_ip) - 1);
            strcpy(log.request_line, "(headers too large)");
            send_error(client_fd, 400, "Bad Request", 0, &log);
            write_log(&log);
            return;
        }

        *term = '\0'; // terminate the header block for string parsing
        keep_alive = handle_one_request(client_fd, buf, client_ip);

        // drop the consumed request
        size_t consumed = (size_t)(term + 4 - buf);
        memmove(buf, buf + consumed, buf_len - consumed);
        buf_len -= consumed;

        req_count++;
        if (!keep_alive || req_count >= MAX_REQUESTS_PER_CONN) return;
    }
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "Options:\n"
        "  -p <port>     Port to listen on (default 8080)\n"
        "  -r <root>     Web root directory (default ./www)\n"
        "  -t <threads>  Number of worker threads (default 8)\n"
        "  -h            Show this help and exit\n",
        prog);
}

static void write_log(const struct log_entry *log) {
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);

    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm);

    fprintf(stderr, "%s - [%s] \"%s\" %d %ld\n", 
            log->client_ip, timebuf, log->request_line, log->status, log->bytes_sent);
}

static void *worker_main(void *arg) {
    struct queue *q = (struct queue *)arg;

    for (;;) {
        int client_fd = queue_dequeue(q);
        if (client_fd == -1) break; // sentinel returned because of interrupt

        // bound each blocking read so a silent client is dropped after the timeout
        struct timeval tv = { KEEPALIVE_TIMEOUT_SECS, 0 };
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        // resolve the client's IP address from the socket
        char client_ip[INET_ADDRSTRLEN];
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        if (getpeername(client_fd, (struct sockaddr *)&addr, &len) == 0) {
            inet_ntop(AF_INET, &addr.sin_addr, client_ip, sizeof(client_ip));
        } else {
            strcpy(client_ip, "?");
        }

        handle_client(client_fd, client_ip);
        close(client_fd);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    int port = 8080;
    int num_workers = 8;

    int opt;
    while ((opt = getopt(argc, argv, "p:r:t:h")) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                if (port < 1 || port > 65535) {
                    fprintf(stderr, "port must be 1-65535, got %d\n", port);
                    exit(1);
                }
                break;
            case 'r':
                web_root = optarg;
                break;
            case 't':
                num_workers = atoi(optarg);
                if (num_workers < 1 || num_workers > MAX_WORKERS) {
                    fprintf(stderr, "threads must be 1-%d, got %d\n", MAX_WORKERS, num_workers);
                    exit(1);
                }
                break;
            case 'h':
                usage(argv[0]);
                exit(0);
            default:
                usage(argv[0]);
                exit(1);
        }
    }
    
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (realpath(web_root, web_root_abs) == NULL) {
        perror("realpath(web_root)");
        exit(1);
    }

    int server_fd = setup_listening_socket(port); 

    struct queue q;
    queue_init(&q);

    pthread_t workers[MAX_WORKERS];
    for (int i = 0; i < num_workers; i++) {
        pthread_create(&workers[i], NULL, worker_main, &q);
    }

    printf("Listening on port %d with %d worker threads. Press Ctrl-C to quit.\n",
        port, num_workers);

    while (!stop_flag) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) { 
            if (errno == EINTR) continue; // signal interrupted accept
            perror("accept"); 
            continue; 
        }
        
        queue_enqueue(&q, client_fd);
    }

    printf("\nShutting down...\n");
    close(server_fd);
    queue_shutdown(&q);

    for (int i = 0; i < num_workers; i++) {
        pthread_join(workers[i], NULL);
    }

    queue_destroy(&q);
    printf("Server exited cleanly.\n");
    
    return 0;
} 