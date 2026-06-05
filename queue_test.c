#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "queue.h"

#define NUM_PRODUCERS  4
#define NUM_CONSUMERS  4
#define OPS_PER_THREAD 1000
#define TOTAL_ITEMS    (NUM_PRODUCERS * OPS_PER_THREAD)

static struct queue q;

/* Each producer enqueues a unique range of integers.
 * Producer i enqueues: i*OPS_PER_THREAD, i*OPS_PER_THREAD+1, ..., (i+1)*OPS_PER_THREAD-1
 * That way each integer in [0, TOTAL_ITEMS) is enqueued exactly once total. */
static void *producer(void *arg) {
    int id = *(int *)arg;
    int base = id * OPS_PER_THREAD;
    for (int i = 0; i < OPS_PER_THREAD; i++) {
        queue_enqueue(&q, base + i);
    }
    return NULL;
}

/* Consumers dequeue items and count how many they each saw.
 * They also record each item in a shared "seen" array so we can
 * verify every item was consumed exactly once. */
static int seen[TOTAL_ITEMS];
static pthread_mutex_t seen_mutex = PTHREAD_MUTEX_INITIALIZER;
static int total_consumed = 0;

static void *consumer(void *arg) {
    (void)arg;
    while (1) {
        pthread_mutex_lock(&seen_mutex);
        if (total_consumed >= TOTAL_ITEMS) {
            pthread_mutex_unlock(&seen_mutex);
            return NULL;
        }
        total_consumed++;
        pthread_mutex_unlock(&seen_mutex);

        int v = queue_dequeue(&q);

        pthread_mutex_lock(&seen_mutex);
        if (v < 0 || v >= TOTAL_ITEMS) {
            fprintf(stderr, "BUG: consumed out-of-range value %d\n", v);
            exit(1);
        }
        seen[v]++;
        pthread_mutex_unlock(&seen_mutex);
    }
}

int main(void) {
    queue_init(&q);

    pthread_t producers[NUM_PRODUCERS];
    pthread_t consumers[NUM_CONSUMERS];
    int prod_ids[NUM_PRODUCERS];

    /* Start all consumers first so they're ready when producers fire. */
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        pthread_create(&consumers[i], NULL, consumer, NULL);
    }
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        prod_ids[i] = i;
        pthread_create(&producers[i], NULL, producer, &prod_ids[i]);
    }

    /* Wait for everyone to finish. */
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pthread_join(producers[i], NULL);
    }
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        pthread_join(consumers[i], NULL);
    }

    /* Verify every item was consumed exactly once. */
    int duplicates = 0, missing = 0;
    for (int i = 0; i < TOTAL_ITEMS; i++) {
        if (seen[i] == 0) missing++;
        else if (seen[i] > 1) duplicates++;
    }

    printf("Produced and consumed %d items.\n", TOTAL_ITEMS);
    printf("Missing: %d, Duplicates: %d\n", missing, duplicates);

    if (missing == 0 && duplicates == 0) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
    }

    queue_destroy(&q);
    return 0;
}