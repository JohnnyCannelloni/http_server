#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include <unistd.h>

#define QUEUE_CAPACITY 64

struct queue {
    int   buffer[QUEUE_CAPACITY];
    int   head;
    int   tail;
    int   count;
    int shutdown; // 0 normally, 1 otherwise 
    pthread_mutex_t mutex;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
};

void queue_init(struct queue *q);
void queue_destroy(struct queue *q);
void queue_enqueue(struct queue *q, int fd);
int  queue_dequeue(struct queue *q);
void  queue_shutdown(struct queue *q);


#endif