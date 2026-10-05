// 320_Lab2_1C_Priority_Inversion.c
// COEN 320 Lab 2 - Part 1: Scenario B (Mutex Protection) + Scenario C (Priority Inversion)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#ifdef __QNX__
#include <sys/neutrino.h>
#endif

#define NUM_ITERATIONS 10000 // adjust to scale the race
#define DUMMY_ITERS    5000  // adjust to scale the per-iteration work

// 1 = run the Scenario C threads on CPU 0 only (Pi 4 has 4 cores; otherwise
//     LP, MP and HP just run in parallel and priorities barely matter)
#define PIN_TO_ONE_CPU 1

// Priorities (max_prio / min_prio are locals set in main before use)
#define HP_PRIO (max_prio - 2) // High Priority
#define MP_PRIO (max_prio - 5) // Medium Priority
#define LP_PRIO (min_prio + 2) // Low Priority (must be lower than MP)

// ---------- Globals ----------
long long shared_counter = 0;
pthread_mutex_t counter_mutex;       // Scenario B
pthread_mutex_t inversion_mutex;     // Scenario C
static volatile uint64_t dummy_sink;
static struct timespec t0;           // start time for timestamps

// ---------- Helpers ----------
static inline void dummy_compute(uint32_t iters) {
    uint64_t x = 0x9e3779b97f4a7c15ULL;
    for (uint32_t i = 0; i < iters; ++i) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
    }
    dummy_sink = x;
}

// Milliseconds since Scenario C started
static long elapsed_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - t0.tv_sec) * 1000L + (now.tv_nsec - t0.tv_nsec) / 1000000L;
}

// Current (effective) priority of the calling thread.
// On QNX, sched_curpriority shows a priority-inheritance boost.
static int cur_prio(void) {
    int policy;
    struct sched_param sp;
    pthread_getschedparam(pthread_self(), &policy, &sp);
#ifdef __QNX__
    return sp.sched_curpriority;
#else
    return sp.sched_priority;
#endif
}

static void pin_to_cpu0(void) {
#if PIN_TO_ONE_CPU && defined(__QNX__)
    ThreadCtl(_NTO_TCTL_RUNMASK, (void *)0x1);
#endif
}

// ---------- Scenario B threads ----------
void *increment_thread_mutex(void *arg) {
    (void)arg;
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        pthread_mutex_lock(&counter_mutex);
        shared_counter++;
        dummy_compute(DUMMY_ITERS);
        pthread_mutex_unlock(&counter_mutex);
    }
    printf("Increment Thread (Mutex) finished. Final counter: %lld\n", shared_counter);
    return NULL;
}

void *decrement_thread_mutex(void *arg) {
    (void)arg;
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        pthread_mutex_lock(&counter_mutex);
        shared_counter--;
        dummy_compute(DUMMY_ITERS);
        pthread_mutex_unlock(&counter_mutex);
    }
    printf("Decrement Thread (Mutex) finished. Final counter: %lld\n", shared_counter);
    return NULL;
}

// ---------- Scenario C threads ----------
void *low_prio_thread(void *arg) {
    (void)arg;
    pin_to_cpu0();
    int id = (int)pthread_self();
    printf("[%4ld ms] LP Thread (%d): Starting. Acquiring mutex...\n", elapsed_ms(), id);
    pthread_mutex_lock(&inversion_mutex);
    for (int i = 0; i < 5; i++) {
        printf("[%4ld ms] LP Thread (%d): Working... %d  (cur prio %d)\n",
               elapsed_ms(), id, i, cur_prio());
        usleep(100000); // Simulate work (0.1 s per iteration)
    }
    printf("[%4ld ms] LP Thread (%d): Releasing mutex. (cur prio %d)\n",
           elapsed_ms(), id, cur_prio());
    pthread_mutex_unlock(&inversion_mutex);
    printf("[%4ld ms] LP Thread (%d): Released. (cur prio %d)\n",
           elapsed_ms(), id, cur_prio());
    return NULL;
}

void *medium_prio_thread(void *arg) {
    (void)arg;
    pin_to_cpu0();
    int id = (int)pthread_self();
    printf("[%4ld ms] MP Thread (%d): Starting. Doing its own work...\n", elapsed_ms(), id);
    for (int i = 0; i < 5; i++) {
        printf("[%4ld ms] MP Thread (%d): Working... %d  (cur prio %d)\n",
               elapsed_ms(), id, i, cur_prio());
        usleep(100000); // Simulate work (0.1 s per iteration)
    }
    printf("[%4ld ms] MP Thread (%d): Finished.\n", elapsed_ms(), id);
    return NULL;
}

void *high_prio_thread(void *arg) {
    (void)arg;
    pin_to_cpu0();
    int id = (int)pthread_self();
    printf("[%4ld ms] HP Thread (%d): Starting. Attempting to acquire mutex...\n",
           elapsed_ms(), id);
    pthread_mutex_lock(&inversion_mutex); // blocks while LP holds the mutex
    printf("[%4ld ms] HP Thread (%d): Mutex acquired. Doing critical work...\n",
           elapsed_ms(), id);
    usleep(100000); // Simulate critical work
    printf("[%4ld ms] HP Thread (%d): Releasing mutex.\n", elapsed_ms(), id);
    pthread_mutex_unlock(&inversion_mutex);
    return NULL;
}

static void create_or_die(pthread_t *tid, pthread_attr_t *attr,
                          void *(*fn)(void *), const char *name) {
    int rc = pthread_create(tid, attr, fn, NULL);
    if (rc != 0) {
        fprintf(stderr, "pthread_create(%s) failed: %s\n", name, strerror(rc));
        exit(EXIT_FAILURE);
    }
}

// ---------- Main ----------
int main(void) {
    pthread_t tid1, tid2;

    // ===== Scenario B =====
    printf("\n--- Scenario B: Mutex Protection ---\n");
    shared_counter = 0;
    pthread_mutex_init(&counter_mutex, NULL);

    create_or_die(&tid1, NULL, increment_thread_mutex, "increment");
    create_or_die(&tid2, NULL, decrement_thread_mutex, "decrement");
    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);

    printf("Main: Final shared_counter with mutex (expected 0): %lld\n", shared_counter);
    pthread_mutex_destroy(&counter_mutex);
    fflush(stdout);

    // ===== Scenario C =====
    printf("\n--- Scenario C: Priority Inversion Observation ---\n");

    // Explicitly request priority inheritance (QNX default, but make it clear)
    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
    pthread_mutex_init(&inversion_mutex, &mattr);
    pthread_mutexattr_destroy(&mattr);

    int max_prio = sched_get_priority_max(SCHED_FIFO);
    int min_prio = sched_get_priority_min(SCHED_FIFO);
    printf("SCHED_FIFO range: %d..%d  ->  LP=%d  MP=%d  HP=%d\n",
           min_prio, max_prio, LP_PRIO, MP_PRIO, HP_PRIO);

    pthread_t lp_tid, mp_tid, hp_tid;
    pthread_attr_t attr;
    struct sched_param param;
    memset(&param, 0, sizeof(param));

    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);

    clock_gettime(CLOCK_MONOTONIC, &t0);

    // Create LP thread
    param.sched_priority = LP_PRIO;
    pthread_attr_setschedparam(&attr, &param);
    create_or_die(&lp_tid, &attr, low_prio_thread, "LP");
    usleep(100000); // Give LP time to acquire mutex

    // Create MP thread
    param.sched_priority = MP_PRIO;
    pthread_attr_setschedparam(&attr, &param);
    create_or_die(&mp_tid, &attr, medium_prio_thread, "MP");
    usleep(100000); // Give MP time to become ready

    // Create HP thread
    param.sched_priority = HP_PRIO;
    pthread_attr_setschedparam(&attr, &param);
    create_or_die(&hp_tid, &attr, high_prio_thread, "HP");

    pthread_join(lp_tid, NULL);
    pthread_join(mp_tid, NULL);
    pthread_join(hp_tid, NULL);

    printf("--- Priority Inversion Scenario Done ---\n");
    pthread_mutex_destroy(&inversion_mutex);
    pthread_attr_destroy(&attr);
    fflush(stdout);

    return EXIT_SUCCESS;
}
