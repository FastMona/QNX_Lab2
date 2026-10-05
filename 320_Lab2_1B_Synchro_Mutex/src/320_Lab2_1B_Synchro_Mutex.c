// 320_Lab2_1B_Synchro_Mutex.c
// COEN 320 Lab 2 - Part 1, Scenario B: Mutex Protection for Shared Resources
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>

#define NUM_ITERATIONS 10000 // adjust to scale the race
#define DUMMY_ITERS    5000  // adjust to scale the per-iteration work

// ---------- Globals (must be declared before any function uses them) ----------
long long shared_counter = 0;        // shared resource
pthread_mutex_t counter_mutex;       // protects shared_counter
static volatile uint64_t dummy_sink; // prevents dummy work being optimized away

// ---------- Helper ----------
static inline void dummy_compute(uint32_t iters) {
    uint64_t x = 0x9e3779b97f4a7c15ULL; // arbitrary non-zero seed
    for (uint32_t i = 0; i < iters; ++i) {
        // small arithmetic mix to keep the ALU busy
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
    }
    dummy_sink = x; // volatile store makes the work observable
}

// ---------- Scenario B threads ----------
void *increment_thread_mutex(void *arg) {
    (void)arg;
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        pthread_mutex_lock(&counter_mutex);   // Acquire lock
        shared_counter++;
        dummy_compute(DUMMY_ITERS);           // controlled dummy work
        pthread_mutex_unlock(&counter_mutex); // Release lock
    }
    printf("Increment Thread (Mutex) finished. Final counter: %lld\n", shared_counter);
    return NULL;
}

void *decrement_thread_mutex(void *arg) {
    (void)arg;
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        pthread_mutex_lock(&counter_mutex);   // Acquire lock
        shared_counter--;
        dummy_compute(DUMMY_ITERS);           // controlled dummy work
        pthread_mutex_unlock(&counter_mutex); // Release lock
    }
    printf("Decrement Thread (Mutex) finished. Final counter: %lld\n", shared_counter);
    return NULL;
}

// ---------- Main ----------
int main(void) {
    pthread_t tid1, tid2;

    printf("\n--- Scenario B: Mutex Protection ---\n");
    shared_counter = 0;                       // Reset counter
    pthread_mutex_init(&counter_mutex, NULL); // Initialize mutex

    pthread_create(&tid1, NULL, increment_thread_mutex, NULL);
    pthread_create(&tid2, NULL, decrement_thread_mutex, NULL);

    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);

    printf("Main: Final shared_counter with mutex (expected 0): %lld\n", shared_counter);
    pthread_mutex_destroy(&counter_mutex);    // Destroy mutex
    fflush(stdout);

    return EXIT_SUCCESS;
}
