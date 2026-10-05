// 320_Lab2_1C_Inversion_Compare.c
// COEN 320 Lab 2 - Part 1, Scenario C (extended):
// Priority inversion with CPU-bound work, run twice:
//   Run 1: mutex protocol PTHREAD_PRIO_NONE    -> unbounded inversion (MP delays HP)
//   Run 2: mutex protocol PTHREAD_PRIO_INHERIT -> LP boosted, HP waits only for LP's critical section
//
// All three threads are pinned to CPU 0 so they actually compete for one core.
#ifdef __linux__
#define _GNU_SOURCE   // only for the Linux test build (CPU affinity); ignored on QNX
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#ifdef __QNX__
#include <sys/neutrino.h>
#endif

// ---- Workload (milliseconds of pure CPU work) ----
#define LP_WORK_MS   300   // LP's critical section (holding the mutex)
#define MP_WORK_MS   500   // MP's own work (no mutex)
#define HP_WORK_MS    50   // HP's critical section
#define CHUNKS         5   // progress lines printed per thread
#define STAGGER_MS    50   // delay between creating LP -> MP -> HP

// ---- Globals ----
static pthread_mutex_t inversion_mutex;
static struct timespec t0;
static volatile uint64_t dummy_sink;
static double iters_per_ms = 0;           // set by calibrate()
static long hp_request_ms, hp_acquire_ms; // for the summary

// ---------- Helpers ----------
static long now_ms(void) {
    struct timespec n;
    clock_gettime(CLOCK_MONOTONIC, &n);
    return (n.tv_sec - t0.tv_sec) * 1000L + (n.tv_nsec - t0.tv_nsec) / 1000000L;
}

static void dummy_compute(uint64_t iters) {
    uint64_t x = 0x9e3779b97f4a7c15ULL;
    for (uint64_t i = 0; i < iters; ++i) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
    }
    dummy_sink = x;
}

// Burn roughly 'ms' of CPU time. Iteration-count based (not wall-clock), so if
// the thread is preempted the work is NOT done while it waits -> delays show up.
static void cpu_work_ms(int ms) { dummy_compute((uint64_t)(ms * iters_per_ms)); }

static void calibrate(void) {
    struct timespec a, b;
    uint64_t n = 2000000;
    clock_gettime(CLOCK_MONOTONIC, &a);
    dummy_compute(n);
    clock_gettime(CLOCK_MONOTONIC, &b);
    double ms = (b.tv_sec - a.tv_sec) * 1000.0 + (b.tv_nsec - a.tv_nsec) / 1e6;
    iters_per_ms = n / ms;
    printf("Calibration: %.0f loop iterations per ms\n", iters_per_ms);
}

static int cur_prio(void) {
    int policy;
    struct sched_param sp;
    pthread_getschedparam(pthread_self(), &policy, &sp);
#ifdef __QNX__
    return sp.sched_curpriority;   // effective priority (shows inheritance boost)
#else
    return sp.sched_priority;
#endif
}

static void pin_to_cpu0(void) {
#if defined(__QNX__)
    if (ThreadCtl(_NTO_TCTL_RUNMASK, (void *)0x1) == -1)
        perror("ThreadCtl(RUNMASK)");
#elif defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#endif
}

// ---------- Threads ----------
static void *low_prio_thread(void *arg) {
    (void)arg;
    pin_to_cpu0();
    printf("[%4ld ms] LP: start, locking mutex (prio %d)\n", now_ms(), cur_prio());
    pthread_mutex_lock(&inversion_mutex);
    for (int i = 0; i < CHUNKS; i++) {
        cpu_work_ms(LP_WORK_MS / CHUNKS);
        printf("[%4ld ms] LP: critical work %d/%d (cur prio %d)\n",
               now_ms(), i + 1, CHUNKS, cur_prio());
    }
    printf("[%4ld ms] LP: unlocking mutex\n", now_ms());
    pthread_mutex_unlock(&inversion_mutex);
    printf("[%4ld ms] LP: done (cur prio %d)\n", now_ms(), cur_prio());
    return NULL;
}

static void *medium_prio_thread(void *arg) {
    (void)arg;
    pin_to_cpu0();
    printf("[%4ld ms] MP: start, CPU-bound work (prio %d)\n", now_ms(), cur_prio());
    for (int i = 0; i < CHUNKS; i++) {
        cpu_work_ms(MP_WORK_MS / CHUNKS);
        printf("[%4ld ms] MP: work %d/%d\n", now_ms(), i + 1, CHUNKS);
    }
    printf("[%4ld ms] MP: done\n", now_ms());
    return NULL;
}

static void *high_prio_thread(void *arg) {
    (void)arg;
    pin_to_cpu0();
    hp_request_ms = now_ms();
    printf("[%4ld ms] HP: start, locking mutex (prio %d)\n", hp_request_ms, cur_prio());
    pthread_mutex_lock(&inversion_mutex);
    hp_acquire_ms = now_ms();
    printf("[%4ld ms] HP: MUTEX ACQUIRED after waiting %ld ms\n",
           hp_acquire_ms, hp_acquire_ms - hp_request_ms);
    cpu_work_ms(HP_WORK_MS);
    pthread_mutex_unlock(&inversion_mutex);
    printf("[%4ld ms] HP: done\n", now_ms());
    return NULL;
}

static void create_or_die(pthread_t *tid, pthread_attr_t *attr, int prio,
                          void *(*fn)(void *), const char *name) {
    struct sched_param p;
    memset(&p, 0, sizeof(p));
    p.sched_priority = prio;
    pthread_attr_setschedparam(attr, &p);
    int rc = pthread_create(tid, attr, fn, NULL);
    if (rc != 0) {
        fprintf(stderr, "pthread_create(%s, prio %d) failed: %s\n", name, prio, strerror(rc));
        exit(EXIT_FAILURE);
    }
}

// ---------- One run with a given mutex protocol ----------
static long run_scenario(int protocol, const char *label) {
    printf("\n=========== %s ===========\n", label);

    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    int rc = pthread_mutexattr_setprotocol(&mattr, protocol);
    if (rc != 0)
        fprintf(stderr, "WARNING: setprotocol(%s) failed: %s\n", label, strerror(rc));
    pthread_mutex_init(&inversion_mutex, &mattr);
    pthread_mutexattr_destroy(&mattr);

    int max_prio = sched_get_priority_max(SCHED_FIFO);
    int min_prio = sched_get_priority_min(SCHED_FIFO);
    int HP_PRIO = max_prio - 2, MP_PRIO = max_prio - 5, LP_PRIO = min_prio + 2;
    printf("LP=%d  MP=%d  HP=%d   (LP holds mutex %d ms, MP works %d ms)\n",
           LP_PRIO, MP_PRIO, HP_PRIO, LP_WORK_MS, MP_WORK_MS);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);

    pthread_t lp, mp, hp;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    create_or_die(&lp, &attr, LP_PRIO, low_prio_thread, "LP");
    usleep(STAGGER_MS * 1000);   // LP grabs the mutex
    create_or_die(&mp, &attr, MP_PRIO, medium_prio_thread, "MP");
    usleep(STAGGER_MS * 1000);   // MP preempts LP on CPU 0
    create_or_die(&hp, &attr, HP_PRIO, high_prio_thread, "HP");

    pthread_join(lp, NULL);
    pthread_join(mp, NULL);
    pthread_join(hp, NULL);

    pthread_attr_destroy(&attr);
    pthread_mutex_destroy(&inversion_mutex);
    fflush(stdout);
    return hp_acquire_ms - hp_request_ms;
}

int main(void) {
    // main is NOT pinned, so it runs on another core and can create the
    // test threads on schedule even while CPU 0 is busy.
    printf("--- Scenario C (extended): Priority Inversion, NONE vs INHERIT ---\n");
    calibrate();

    long wait_none    = run_scenario(PTHREAD_PRIO_NONE,    "Run 1: PTHREAD_PRIO_NONE (no inheritance)");
    long wait_inherit = run_scenario(PTHREAD_PRIO_INHERIT, "Run 2: PTHREAD_PRIO_INHERIT");

    printf("\n=========== Summary ===========\n");
    printf("HP blocked-on-mutex time, PRIO_NONE    : %4ld ms\n", wait_none);
    printf("HP blocked-on-mutex time, PRIO_INHERIT : %4ld ms\n", wait_inherit);
    printf("Expected: NONE ~ remaining MP work + remaining LP work; "
           "INHERIT ~ remaining LP work only\n");
    return EXIT_SUCCESS;
}
