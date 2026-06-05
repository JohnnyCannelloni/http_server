# multi-threaded HTTP server in C

A from-scratch HTTP/1.0 file server using POSIX sockets and a
pthread worker pool with a bounded ring-buffer task queue.

## Status

- Phase 1: single-threaded, parses requests, serves files, proper HTTP status codes.
- Phase 2: multi-threaded via fixed pool of pthread workers and producer-consumer queue.

## Build and run

    gcc -Wall -Wextra -pthread server.c queue.c -o server
    mkdir -p www && echo "<h1>Hello</h1>" > www/index.html
    ./server

## Benchmark

ab -n 10000 -c 100: ~11k req/sec on localhost (M-series Mac).

## Architecture

- Main thread accepts connections and enqueues client file descriptors.
- 8 worker threads consume from the queue and serve requests in parallel.
- Queue is bounded (64 slots), protected by a mutex and two condition variables
  ("not empty" and "not full") implementing the textbook producer-consumer pattern.

## Known limitations (TODO)

- Content-Type is always text/html (MIME detection planned).
- No directory traversal protection.
- No graceful shutdown.
- Ignores partial reads/writes.
- HTTP/1.0 only (no keep-alive).
- No access logging.