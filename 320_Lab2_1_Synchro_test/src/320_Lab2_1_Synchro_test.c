// 320_Lab2_1_Synchro_test.c
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#define NUM_ITERATIONS 10000 // adjust to scale the race
#define DUMMY_ITERS 5000 // adjust to scale the per-iteration work
// Shared global variable
long long shared_counter = 0;
static volatile uint64_t dummy_sink; // prevents optimization away
static inline void dummy_compute(uint32_t iters) {
 uint64_t x = 0x9e3779b97f4a7c15ULL; // arbitrary non-zero seed
 for (uint32_t i = 0; i < iters; ++i) {
 // small arithmetic mix to keep the ALU busy
 x ^= x << 13;
 x ^= x >> 7;
 x ^= x << 17;
 }
 // volatile store makes the loop's work observable to the compiler
 dummy_sink = x;
}
void *increment_thread(void *arg) {
 for (int i = 0; i < NUM_ITERATIONS; i++) {
 int count = shared_counter;
 count++;
 shared_counter = count;
 dummy_compute(DUMMY_ITERS); // controlled dummy work
 }
 printf("Increment Thread finished. Final counter: %lld\n",
shared_counter);
 return NULL;
}
void *decrement_thread(void *arg) {
 for (int i = 0; i < NUM_ITERATIONS; i++) {
 int count = shared_counter;
 count--;
 shared_counter = count;
dummy_compute(DUMMY_ITERS); // controlled dummy work
 }
 printf("Decrement Thread finished. Final counter: %lld\n",
shared_counter);
 return NULL;
}
int main(void) {
 pthread_t tid1, tid2;
 printf("--- Scenario A: Race Condition Demonstration ---\n");
 shared_counter = 0; // Reset counter
 pthread_create(&tid1, NULL, increment_thread, NULL);
 pthread_create(&tid2, NULL, decrement_thread, NULL);
 pthread_join(tid1, NULL);
 pthread_join(tid2, NULL);
 printf("Main: Final shared_counter (expected 0): %lld\n",
shared_counter);
 fflush(stdout); // Ensure output order for observation
 // ... (Code for Scenario B will go here) ...
 return EXIT_SUCCESS;
}
