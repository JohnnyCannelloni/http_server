# BareHTTP

A multi-threaded HTTP/1.1 file server written in C, with keep-alive support.

I built this to learn how an HTTP server actually works at every layer,
from the TCP three-way handshake up through request parsing and response
formatting. It uses no libraries beyond the POSIX standard.

## Build and run

```
gcc -Wall -Wextra -pthread server.c queue.c -o server
./server
```

The server listens on port 8080 by default and serves files from `./www`.
Open `http://localhost:8080/` in a browser, or use curl:

```
curl http://localhost:8080/index.html
```

## Options

```
./server [-p port] [-r web_root] [-t threads] [-h]
```

- `-p` port to listen on (default 8080)
- `-r` directory to serve files from (default ./www)
- `-t` number of worker threads (default 8)
- `-h` print help and exit

## Architecture

The server uses a fixed pool of worker threads and a bounded task queue.

The main thread does only one thing: it calls `accept()` in a loop and
submits each accepted client file descriptor to the queue. Worker threads
pull from the queue and handle the actual request processing in parallel.

This is the standard producer-consumer pattern. The queue is protected by
a mutex and uses two condition variables, one for "not empty" (consumers
wait on it) and one for "not full" (the producer waits on it). Each
operation signals the opposite condition after modifying the queue.

```
client ---> accept ---> [task queue] ---> worker ---> handle request <--+
                                              |            |             |
                                              |            +---> write response
                                              |            |             |
                                              |      keep-alive? --------+
                                              |            | no
                                              +----- close socket
```

A request goes through these steps inside a worker:

1. Read from the client socket into a buffer until the `\r\n\r\n` header
   terminator arrives (only GET is supported, so there is no request body).
2. Parse the request line with `sscanf` using bounded-length specifiers.
3. Decide keep-alive: HTTP/1.1 stays open unless `Connection: close`; HTTP/1.0
   closes unless `Connection: keep-alive`.
4. Reject non-GET methods with 405.
5. Build the filesystem path. If the URL ends with `/`, append `index.html`.
6. Resolve the path with `realpath`. Reject paths outside the web root with 403.
7. Open the file. Reject directories or missing files with 404.
8. Build HTTP response headers (status line, Content-Type, Content-Length, Connection).
9. Send headers, then stream the file body in 4 KB chunks.
10. If keep-alive, loop back for the next request on the same connection;
    otherwise close the socket.

## Keep-alive

The server speaks HTTP/1.1 and reuses a single TCP connection for multiple
requests. A worker keeps reading requests off one connection until the client
sends `Connection: close`, goes idle, or hits the per-connection request cap.

Because the worker pool is a fixed size, an idle kept-alive connection would
otherwise pin a thread indefinitely. Two hardcoded bounds prevent that:

- **Idle timeout** — each client socket gets a `SO_RCVTIMEO` of 5 seconds, so a
  silent connection is dropped rather than holding a worker forever.
- **Request cap** — a single connection serves at most 100 requests before the
  server closes it, so no one client can monopolize a worker.

Partial reads and pipelined requests are handled by buffering per connection:
each fully received request is consumed from the buffer and any leftover bytes
are carried into the next iteration.

## Graceful shutdown

The server installs handlers for SIGINT and SIGTERM. When a signal arrives,
the handler sets a flag. The main thread sees the flag, exits the accept
loop, closes the listening socket, and signals the queue to shut down.

Worker threads finish whatever request they were serving, then dequeue
remaining items from the queue and serve those too. Only when the queue is
empty AND in shutdown mode do workers exit. The main thread joins all
workers before exiting itself.

This means no in-flight request is dropped and no file descriptor is leaked
during shutdown. A worker sitting inside a live keep-alive connection only
returns to the queue once that client closes or hits the 5-second idle timeout,
so shutdown can lag by up to that timeout — but nothing is dropped.

## Concurrency

What's shared between threads:

- The task queue (mutex-protected).
- The web root path (set once at startup, never modified).
- The stop flag (volatile sig_atomic_t).

Everything else, including the request buffer and parsed fields, is local
to the worker thread handling that request. The HTTP processing functions
have no shared state and need no synchronization.

## MIME types

A small table maps file extensions to MIME types:

| Extension | Type |
|-----------|------|
| .html, .htm | text/html |
| .css | text/css |
| .js | application/javascript |
| .json | application/json |
| .png | image/png |
| .jpg, .jpeg | image/jpeg |
| .gif | image/gif |
| .svg | image/svg+xml |
| .txt | text/plain |
| .ico | image/x-icon |
| anything else | application/octet-stream |

Extensions are matched case-insensitively.

## Logging

Each request produces one line on stderr:

```
127.0.0.1 - [2026-06-15 18:42:11] "GET /index.html HTTP/1.0" 200 1234
```

The fields are client IP, timestamp, request line, status code, and bytes
sent in the body.

## Files

- `server.c` - main, accept loop, request handling, signal handling, CLI parsing
- `queue.c` / `queue.h` - thread-safe bounded queue
- `www/` - default web root with a small landing page

## Known limitations

The server is deliberately minimal. Things it does NOT do:

- Chunked transfer encoding.
- TLS / HTTPS.
- Range requests (no resumed downloads).
- Conditional requests (no If-Modified-Since, no ETags).
- Compression (gzip, brotli).
- Request size limits beyond the 4 KB initial read buffer.
- HEAD or OPTIONS methods. Only GET is supported.
- IPv6. Only IPv4.

These are all reasonable next steps but were out of scope.

## What I learned building this

A few things that surprised me or stuck:

- TCP and HTTP are completely separate layers. The kernel handles TCP
  entirely on your behalf; HTTP is just text you read and write over the
  byte stream the kernel gives you.
- `read()` and `write()` can return fewer bytes than requested. This is
  fine on localhost but matters on real networks.
- Signal handlers can do almost nothing. You can set a flag and that's
  about it. All the real shutdown logic runs in the main thread.
- The biggest lesson from concurrency was the discipline of "what state
  is shared, and what protects it." Most of the code is single-threaded
  by design; only the queue needs synchronization.
- Directory traversal (`/../../etc/passwd`) is trivial to exploit on a
  naive file server. `realpath` plus a prefix check is the standard
  defense.
