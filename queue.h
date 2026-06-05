#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>

#define QUEUE_CAPACITY 64

struct queue {
    int   buffer[QUEUE_CAPACITY];
    int   head;
    int   tail;
    int   count;
    pthread_mutex_t mutex;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
};

void queue_init(struct queue *q);
void queue_destroy(struct queue *q);
void queue_enqueue(struct queue *q, int fd);
int  queue_dequeue(struct queue *q);

#endif