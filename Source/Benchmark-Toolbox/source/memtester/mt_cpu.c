#include "mt_cpu.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "mt_tests.h"

#define MT_MAX_THREADS 4
#define MT_PAGESIZE 4096
#define MT_MEMSHIFT 20
#define MT_WANTRAW 2048
#define MT_WORKER_PRIO 0x3B

typedef unsigned long ul;
typedef unsigned long volatile ulv;
typedef unsigned long long ull;

typedef struct {
    int idx;
    int burnin;
    int burn_kernel;
    void volatile *aligned;
    size_t words;
    volatile uint64_t loop;
    volatile uint64_t iters;
    volatile uint64_t mismatches;
} mt_worker_t;

static Thread s_coord;
static Thread s_tobj[MT_MAX_THREADS];
static void volatile *s_buf[MT_MAX_THREADS];
static mt_worker_t s_workers[MT_MAX_THREADS];

static volatile bool s_stop = false;
static volatile bool s_running = false;
static volatile bool s_done = false;
static volatile uint64_t s_total_mb = 0;
static volatile int s_nthreads = 0;
static int s_mode = 0;
static bool s_coord_open = false;
static const char *volatile s_cur_test = "Idle";
static volatile int s_step = 0;

#define MT_TESTS_COUNT 15

static int run_sequence(mt_worker_t *w) {
    size_t words = w->words;
    size_t half = words / 2;
    ulv *bufa = (ulv *)w->aligned;
    ulv *bufb = (ulv *)((size_t)w->aligned + (words / 2) * sizeof(ul));

    if (w->idx == 0) {
        s_cur_test = "Stuck Address";
        s_step = 0;
    }
    if (mt_test_stuck_address((unsigned long volatile *)w->aligned, words))
        w->mismatches++;

    for (int t = 0; mt_tests[t].name; t++) {
        if (s_stop)
            return 0;
        if (w->idx == 0) {
            s_cur_test = mt_tests[t].name;
            s_step = t + 1;
        }
        if (mt_tests[t].fp(bufa, bufb, half))
            w->mismatches++;
    }
    return 0;
}

static void burn_kernel_run(int kernel, void *a, void *b, size_t bytes, int pattern) {
    switch (kernel) {
        case 0:
            memcpy(a, b, bytes);
            break;
        case 1:
            memset(a, 0x00, bytes);
            break;
        default:
            memset(b, pattern, bytes);
            break;
    }
}

static int run_burnin(mt_worker_t *w) {
    size_t bytes = (w->words / 2) * sizeof(ul);
    void *a = (void *)w->aligned;
    void *b = (void *)((size_t)w->aligned + bytes);
    int pattern = (w->idx & 1) ? 0x55 : 0xaa;
    int kernel = w->burn_kernel;

    int reps = 1;
    while (!s_stop) {
        u64 t0 = armGetSystemTick();
        for (int i = 0; i < reps; i++) {
            if (s_stop)
                return 0;
            burn_kernel_run(kernel, a, b, bytes, pattern);
        }
        double sec = (double)armTicksToNs(armGetSystemTick() - t0) / 1000000000.0;
        if (sec >= 0.25 || reps > 0xfffff)
            break;

        int grown = (reps + 1 < reps * 2) ? reps * 2 : reps + 1;
        int next = reps * 2;
        if (sec > 0.0) {
            next = (int)((0.25 / sec) * (double)reps);
            if (next < grown)
                next = grown;
        }
        reps = next;
    }

    w->burn_kernel = (kernel + 1) % 3;
    return 0;
}

static void worker_main(void *arg) {
    mt_worker_t *w = (mt_worker_t *)arg;
    while (!s_stop) {
        if (w->burnin)
            run_burnin(w);
        else
            run_sequence(w);
        w->iters++;
        if (!w->burnin)
            w->loop++;
    }
}

static void coordinator(void *arg) {
    (void)arg;

    ull totalmem = 0;
    int testThreads = 3;
    void volatile *probe[MT_MAX_THREADS];
    size_t want[MT_MAX_THREADS];
    int numMallocs = 0;
    size_t wantbytes_orig = ((size_t)MT_WANTRAW << MT_MEMSHIFT);
    ptrdiff_t pagemask = (ptrdiff_t)~((size_t)MT_PAGESIZE - 1);

    for (int div = 0; div <= 3; div++) {
        probe[div] = NULL;
        want[div] = wantbytes_orig;
        while (!probe[div] && want[div]) {
            probe[div] = (void volatile *)malloc(want[div]);
            if (!probe[div])
                want[div] -= MT_PAGESIZE;
        }
        totalmem += want[div];
        if ((want[div] >> MT_MEMSHIFT) < (MT_WANTRAW - 1)) {
            numMallocs = div + 1;
            break;
        }
        if (div == 3)
            numMallocs = 4;
    }
    for (int div = 0; div < numMallocs; div++)
        free((void *)probe[div]);

    bool devkit8gb = false;
    if ((totalmem >> MT_MEMSHIFT) > 3 * (MT_WANTRAW - 1)) {
        devkit8gb = true;
        testThreads = 4;
    }

    ull stack_reserve = (ull)(0x4000 + MT_PAGESIZE) * testThreads;
    if (totalmem > stack_reserve)
        totalmem -= stack_reserve;
    s_nthreads = testThreads;

    // Combined memtester + BW burn-in uses small per-thread buffers (the RAM
    // pressure comes from the burn-in's continuous bandwidth, not capacity), so
    // the single memtester thread's loops complete quickly. Full memtester mode
    // maps essentially all of RAM.
    size_t combined_each = (size_t)testThreads << 25;  // threads * 32 MB

    // Create the worker threads BEFORE allocating the (potentially RAM-filling)
    // test buffers. libnx allocates each thread's stack from the same heap, so
    // doing this after the buffers would leave nothing for the stacks and
    // threadCreate would fail, ending the run immediately.
    bool created[MT_MAX_THREADS] = { false };
    for (int div = 0; div < testThreads; div++) {
        s_buf[div] = NULL;
        s_workers[div].idx = div;
        s_workers[div].burnin = (s_mode == 1 && div != 0) ? 1 : 0;
        s_workers[div].burn_kernel = 0;
        s_workers[div].aligned = NULL;
        s_workers[div].words = 0;
        s_workers[div].loop = 0;
        s_workers[div].iters = 0;
        s_workers[div].mismatches = 0;
        if (R_SUCCEEDED(threadCreate(&s_tobj[div], worker_main, &s_workers[div], NULL, 0x4000, MT_WORKER_PRIO, div == 3 ? -2 : div)))
            created[div] = true;
    }

    for (int div = 0; div < testThreads; div++) {
        if (!created[div])
            continue;
        void volatile *buf = NULL;
        size_t w;
        if (s_mode == 1)
            w = combined_each;
        else if (devkit8gb)
            w = (div != 3) ? (totalmem / 3) : ((size_t)(MT_WANTRAW - 1) << MT_MEMSHIFT);
        else
            w = totalmem / testThreads;

        while (!buf && w) {
            buf = (void volatile *)malloc(w);
            if (!buf)
                w -= MT_PAGESIZE;
        }
        s_buf[div] = buf;

        size_t bufsize = w;
        void volatile *aligned;
        if ((size_t)buf % MT_PAGESIZE) {
            aligned = (void volatile *)(((size_t)buf & pagemask) + MT_PAGESIZE);
            bufsize -= ((size_t)aligned - (size_t)buf);
        } else {
            aligned = buf;
        }

        s_workers[div].aligned = aligned;
        s_workers[div].words = bufsize / sizeof(ul);
        s_total_mb += (uint64_t)(bufsize >> MT_MEMSHIFT);
    }

    s_cur_test = "Stuck Address";
    s_step = 0;

    bool started[MT_MAX_THREADS] = { false };
    for (int div = 0; div < testThreads; div++) {
        if (!created[div])
            continue;
        if (R_SUCCEEDED(threadStart(&s_tobj[div])))
            started[div] = true;
    }

    for (int div = 0; div < testThreads; div++) {
        if (!created[div])
            continue;
        if (started[div])
            threadWaitForExit(&s_tobj[div]);
        threadClose(&s_tobj[div]);
    }

    for (int div = 0; div < testThreads; div++) {
        if (s_buf[div])
            free((void *)s_buf[div]);
        s_buf[div] = NULL;
    }

    s_done = true;
    s_running = false;
}

void mt_cpu_start(int mode) {
    if (s_running)
        return;
    s_mode = mode;
    s_stop = false;
    mt_abort = 0;
    s_done = false;
    s_total_mb = 0;
    s_nthreads = 0;
    s_workers[0].loop = 0;
    s_cur_test = "Preparing...";
    s_running = true;
    appletSetAutoSleepDisabled(true);

    if (s_coord_open) {
        threadWaitForExit(&s_coord);
        threadClose(&s_coord);
        s_coord_open = false;
    }

    if (R_FAILED(threadCreate(&s_coord, coordinator, NULL, NULL, 0x4000, MT_WORKER_PRIO, -2))) {
        s_running = false;
        return;
    }
    s_coord_open = true;
    threadStart(&s_coord);
}

void mt_cpu_stop(void) {
    s_stop = true;
    mt_abort = 1;
    if (s_coord_open) {
        threadWaitForExit(&s_coord);
        threadClose(&s_coord);
        s_coord_open = false;
    }
    s_running = false;
    appletSetAutoSleepDisabled(false);
}

int mt_cpu_running(void) {
    return s_running ? 1 : 0;
}

void mt_cpu_get(mt_cpu_status_t *out) {
    if (!out)
        return;
    uint64_t burnin = 0, mism = 0;
    int n = s_nthreads;
    for (int i = 0; i < n; i++) {
        mism += s_workers[i].mismatches;
        if (s_workers[i].burnin)
            burnin += s_workers[i].iters;
    }
    out->loop = s_workers[0].loop;
    out->test = s_cur_test;
    out->error = mism > 0 ? 1 : 0;
    out->mismatches = mism;
    out->burnin_iters = burnin;
    out->done = s_done;
    out->total_mb = s_total_mb;
    out->threads = s_nthreads;
    int step = s_step;
    if (step < 0)
        step = 0;
    if (step > MT_TESTS_COUNT)
        step = MT_TESTS_COUNT;
    out->progress = (float)step / (float)MT_TESTS_COUNT;
}
