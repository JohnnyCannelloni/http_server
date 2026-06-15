#include "queue.h"

void queue_init(struct queue *q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->shutdown = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

void queue_destroy(struct queue *q) {
    pthread_cond_destroy(&q->not_full);
    pthread_cond_destroy(&q->not_empty);
    pthread_mutex_destroy(&q->mutex);
}

void queue_enqueue(struct queue *q, int fd) {
    pthread_mutex_lock(&q->mutex);

    while (q->count == QUEUE_CAPACITY && !q->shutdown) {
        pthread_cond_wait(&q->not_full, &q->mutex);
    }

    if (q->shutdown) {
        pthread_mutex_unlock(&q->mutex);
        close(fd);
        return;
    }

    q->buffer[q->tail] = fd;
    q->tail = (q->tail + 1) % QUEUE_CAPACITY;
    q->count++;

    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
}

int queue_dequeue(struct queue *q) {
    pthread_mutex_lock(&q->mutex);

    while (q->count == 0 && !q->shutdown) {
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }

    if (q->count == 0) { // the loop terminated because of the shutdown flag - no drain needed
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }

    // normal dequue path (wether the shutdown is active or not in order to drain remaining requests)
    int fd = q->buffer[q->head];
    q->head = (q->head + 1) % QUEUE_CAPACITY;
    q->count--;

    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);

    return fd;
}

void queue_shutdown(struct queue *q) {
    pthread_mutex_lock(&q->mutex);

    q->shutdown = 1;

    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);

    pthread_mutex_unlock(&q->mutex);
}