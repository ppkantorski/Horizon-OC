#include "cpu_stress.h"

#include <stdbool.h>
#include <stdlib.h>
#include <switch.h>

#define CS_N 128
#define CS_THREADS 3
#define CS_PRIO 0x3B
#define CS_STACK 0x10000

typedef struct {
    int core;
    volatile uint64_t iters;
    volatile uint64_t mismatches;
} cs_worker_t;

static Thread s_t[CS_THREADS];
static bool s_started[CS_THREADS];
static cs_worker_t s_w[CS_THREADS];

static volatile bool s_stop = false;
static volatile bool s_running = false;
static int s_mode = 0;
static volatile int s_nthreads = 0;

static volatile long double s_sink;

static void mwc_seed(uint32_t *w, uint32_t *z, int core) {
    uint32_t mix = (uint32_t)core * 0x9e3779b9u;
    uint32_t ww = 0xdeadbeefu ^ mix;
    uint32_t zz = 0x12345678u ^ ~mix;
    *w = ww ? ww : 0xdeadbeefu;
    *z = zz ? zz : 0x12345678u;
}

static inline uint32_t mwc32(uint32_t *w, uint32_t *z) {
    *z = 36969u * (*z & 65535u) + (*z >> 16);
    *w = 18000u * (*w & 65535u) + (*w >> 16);
    return (*z << 16) + *w;
}

static void matrixprod(long double *a, long double *b, long double *r, uint32_t *w, uint32_t *z) {
    const long double v = 1.0L / (long double)((uint32_t)~0u);

    for (int i = 0; i < CS_N; i++) {
        for (int j = 0; j < CS_N; j++) {
            a[i * CS_N + j] = (long double)mwc32(w, z) * v;
            b[i * CS_N + j] = (long double)mwc32(w, z) * v;
            r[i * CS_N + j] = 0.0L;
        }
    }

    for (int i = 0; i < CS_N && !s_stop; i++) {
        for (int j = 0; j < CS_N; j++) {
            for (int k = 0; k < CS_N; k++)
                r[i * CS_N + j] += a[i * CS_N + k] * b[k * CS_N + j];
        }
    }

    long double sum = 0.0L;
    for (int i = 0; i < CS_N; i++)
        for (int j = 0; j < CS_N; j++)
            sum += r[i * CS_N + j];
    s_sink = sum;
}

static uint32_t hanoi_rec(int n) {
    if (n == 0)
        return 1;
    if (n == 1)
        return 2;
    return hanoi_rec(n - 1) + hanoi_rec(n - 1);
}

static int hanoi_pass(void) {
    uint32_t sum = 0;
    for (int k = 19; k >= 0; k--)
        sum += hanoi_rec(k);
    return sum == 0xfffffu;
}

static void cs_worker(void *arg) {
    cs_worker_t *c = (cs_worker_t *)arg;
    uint32_t w, z;
    mwc_seed(&w, &z, c->core);

    long double *a = NULL, *b = NULL, *r = NULL;
    if (s_mode == 0) {
        size_t bytes = sizeof(long double) * CS_N * CS_N;
        a = (long double *)malloc(bytes);
        b = (long double *)malloc(bytes);
        r = (long double *)malloc(bytes);
        if (!a || !b || !r) {
            free(a);
            free(b);
            free(r);
            return;
        }
    }

    while (!s_stop) {
        if (s_mode == 0) {
            matrixprod(a, b, r, &w, &z);
        } else {
            if (!hanoi_pass())
                c->mismatches++;
        }
        c->iters++;
    }

    free(a);
    free(b);
    free(r);
}

void cpu_stress_start(int mode) {
    if (s_running)
        return;
    s_mode = mode;
    s_stop = false;
    s_running = true;
    s_nthreads = CS_THREADS;
    appletSetAutoSleepDisabled(true);

    int started = 0;
    for (int i = 0; i < CS_THREADS; i++) {
        s_started[i] = false;
        s_w[i].core = i;
        s_w[i].iters = 0;
        s_w[i].mismatches = 0;
        if (R_FAILED(threadCreate(&s_t[i], cs_worker, &s_w[i], NULL, CS_STACK, CS_PRIO, i)))
            continue;
        if (R_SUCCEEDED(threadStart(&s_t[i]))) {
            s_started[i] = true;
            started++;
        } else {
            threadClose(&s_t[i]);
        }
    }

    if (started == 0) {
        s_running = false;
        appletSetAutoSleepDisabled(false);
    }
}

void cpu_stress_stop(void) {
    s_stop = true;
    for (int i = 0; i < CS_THREADS; i++) {
        if (!s_started[i])
            continue;
        threadWaitForExit(&s_t[i]);
        threadClose(&s_t[i]);
        s_started[i] = false;
    }
    s_running = false;
    appletSetAutoSleepDisabled(false);
}

int cpu_stress_running(void) {
    return s_running ? 1 : 0;
}

void cpu_stress_get(cpu_stress_status_t *out) {
    if (!out)
        return;
    uint64_t it = 0, mm = 0;
    int n = s_nthreads;
    for (int i = 0; i < n; i++) {
        it += s_w[i].iters;
        mm += s_w[i].mismatches;
    }
    out->iters = it;
    out->mismatches = mm;
    out->threads = s_nthreads;
    out->running = s_running ? 1 : 0;
}
