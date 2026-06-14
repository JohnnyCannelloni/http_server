A from-scratch HTTP/1.0 file server using POSIX sockets and a
pthread worker pool with a bounded ring-buffer task queue.

## Build and run

    gcc -Wall -Wextra -pthread server.c queue.c -o server
    ./server

## Benchmark

ab -n 10000 -c 100: ~11k req/sec on localhost.

## Architecture

- Main thread accepts connections and enqueues client file descriptors.
- 8 worker threads consume from the queue and serve requests in parallel.
- Queue is bounded (64 slots), protected by a mutex and two condition variables.

