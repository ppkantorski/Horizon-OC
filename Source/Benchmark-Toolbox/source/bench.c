/*
 * bench.c — CPU bandwidth + latency benchmarks and system info.
 * Refactored from Membench-NX/main.c into result-returning functions with no
 * console I/O, so a GUI (borealis) can drive them from a worker thread.
 *
 * Original bandwidth/latency methodology:
 *   Copyright (c) 2011 Siarhei Siamashka, (c) 20xx KazushiMe, (c) 2025 Souldbminer
 */

#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "bench.h"
#include "gpu_bw.h"
#include <sys/time.h>

#define SIZE (32 * 1024 * 1024)
#define MAXREPEATS 10
#define LATBENCH_COUNT 10000000
#define ALIGN_PADDING 0x100000
#define CACHE_LINE_SIZE 128

struct f_data {
    void (*func)(int64_t *, int64_t *, int);
    int64_t *arg1;
    int64_t *arg2;
    int arg3;
};

static pthread_cond_t p_ready, p_start;
static pthread_mutex_t p_lock;
static pthread_t *p_worker = NULL;
static struct f_data *worker_data = NULL;
static int p_worker_not_ready, p_workers_ready;

static void *thread_func(void *data) {
    struct f_data *d = data;
    pthread_mutex_lock(&p_lock);
    p_worker_not_ready--;
    if (!p_worker_not_ready)
        pthread_cond_signal(&p_ready);
    while (p_workers_ready != 1)
        pthread_cond_wait(&p_start, &p_lock);
    pthread_mutex_unlock(&p_lock);
    (d->func)(d->arg1, d->arg2, d->arg3);
    pthread_exit(NULL);
}

static void parallel_run(void) {
    pthread_mutex_lock(&p_lock);
    p_workers_ready = 1;
    pthread_mutex_unlock(&p_lock);
    pthread_cond_broadcast(&p_start);
}

static void parallel_init(int threads) {
    pthread_attr_t attr;
    pthread_cond_init(&p_ready, NULL);
    pthread_cond_init(&p_start, NULL);
    pthread_mutex_init(&p_lock, NULL);
    p_worker_not_ready = threads;
    p_workers_ready = 0;
    pthread_attr_init(&attr);
    if (!p_worker || !worker_data) {
        p_worker = malloc(threads * sizeof(pthread_t));
        worker_data = malloc(threads * sizeof(struct f_data));
    }
    for (int i = 0; i < threads; i++)
        pthread_create(p_worker + i, &attr, thread_func, worker_data + i);
    pthread_mutex_lock(&p_lock);
    while (p_worker_not_ready != 0)
        pthread_cond_wait(&p_ready, &p_lock);
    pthread_mutex_unlock(&p_lock);
}

static void aligned_block_copy(int64_t *__restrict dst_, int64_t *__restrict src, int size) {
    volatile int64_t *dst = dst_;
    int64_t t1, t2, t3, t4;
    while ((size -= 64) >= 0) {
        t1 = *src++;
        t2 = *src++;
        t3 = *src++;
        t4 = *src++;
        *dst++ = t1;
        *dst++ = t2;
        *dst++ = t3;
        *dst++ = t4;
        t1 = *src++;
        t2 = *src++;
        t3 = *src++;
        t4 = *src++;
        *dst++ = t1;
        *dst++ = t2;
        *dst++ = t3;
        *dst++ = t4;
    }
}

static void aligned_block_fetch(int64_t *__restrict dst, int64_t *__restrict src_, int size) {
    volatile int64_t *src = src_;
    (void)dst;
    while ((size -= 64) >= 0) {
        *src++;
        *src++;
        *src++;
        *src++;
        *src++;
        *src++;
        *src++;
        *src++;
    }
}

static void aligned_block_fill(int64_t *__restrict dst_, int64_t *__restrict src, int size) {
    volatile int64_t *dst = dst_;
    int64_t data = *src;
    while ((size -= 64) >= 0) {
        *dst++ = data;
        *dst++ = data;
        *dst++ = data;
        *dst++ = data;
        *dst++ = data;
        *dst++ = data;
        *dst++ = data;
        *dst++ = data;
    }
}

static double gettime(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)((int64_t)tv.tv_sec * 1000000 + tv.tv_usec) / 1000000.;
}

static double bandwidth_bench_helper(int threads, int64_t *dstbuf, int64_t *srcbuf, int size, void (*f)(int64_t *, int64_t *, int)) {
    int i, loopcount, innerloopcount, n;
    double t, t1, t2, speed, maxspeed, s, s0, s1, s2;

    s = s0 = s1 = s2 = 0.;
    maxspeed = 0.;
    for (n = 0; n < MAXREPEATS; n++) {
        loopcount = 0;
        innerloopcount = 1;
        t = 0.;
        do {
            loopcount += innerloopcount;
            for (i = 0; i < innerloopcount; i++) {
                parallel_init(threads);
                for (int pt = 0; pt < threads; pt++) {
                    (worker_data + pt)->func = f;
                    (worker_data + pt)->arg1 = dstbuf + size * pt / sizeof(int64_t);
                    (worker_data + pt)->arg2 = srcbuf + size * pt / sizeof(int64_t);
                    (worker_data + pt)->arg3 = size;
                }
                t1 = gettime();
                parallel_run();
                for (int pt = 0; pt < threads; pt++)
                    pthread_join(p_worker[pt], NULL);
                t2 = gettime();
                t += t2 - t1;
            }
            innerloopcount *= 2;
        } while (t < 0.5);

        speed = (double)size * threads * loopcount / t / 1000000.;
        s0 += 1.;
        s1 += speed;
        s2 += speed * speed;
        if (speed > maxspeed)
            maxspeed = speed;
        if (s0 > 2.) {
            s = sqrt((s0 * s2 - s1 * s1) / (s0 * (s0 - 1)));
            if (s < maxspeed / 1000.)
                break;
        }
    }
    return maxspeed;
}

static char *align_up(char *ptr, int align) {
    return (char *)(((uintptr_t)ptr + align - 1) & ~(uintptr_t)(align - 1));
}

static void *alloc_nonaliased_buffers(void **buf1_, int size1, void **buf2_, int size2, void **buf3_, int size3) {
    char **buf1 = (char **)buf1_, **buf2 = (char **)buf2_, **buf3 = (char **)buf3_;
    int mask = (ALIGN_PADDING - 1) & ~(CACHE_LINE_SIZE - 1);
    char *buf = malloc(size1 + size2 + size3 + 9 * ALIGN_PADDING);
    char *ptr = buf;
    memset(buf, 0xCC, size1 + size2 + size3 + 9 * ALIGN_PADDING);
    ptr = align_up(ptr, ALIGN_PADDING);
    if (buf1) {
        *buf1 = ptr + (0xAAAAAAAA & mask);
        ptr = align_up(*buf1 + size1, ALIGN_PADDING);
    }
    if (buf2) {
        *buf2 = ptr + (0x55555555 & mask);
        ptr = align_up(*buf2 + size2, ALIGN_PADDING);
    }
    if (buf3) {
        *buf3 = ptr + (0xCCCCCCCC & mask);
    }
    return buf;
}

#pragma GCC diagnostic push
static void __attribute__((noinline)) random_read_test(char *buf, int count, int nbits) {
    uint32_t seed = 0;
    uintptr_t mask = (1 << nbits) - 1;
    uint32_t v;
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
    static volatile uint32_t dummy;
#define RMA()                         \
    seed = seed * 1103515245 + 12345; \
    v = (seed >> 16) & 0xFF;          \
    seed = seed * 1103515245 + 12345; \
    v |= (seed >> 8) & 0xFF00;        \
    seed = seed * 1103515245 + 12345; \
    v |= seed & 0x7FFF0000;           \
    seed |= buf[v & mask];
    while (count >= 16) {
        RMA() RMA() RMA() RMA() RMA() RMA() RMA() RMA() RMA() RMA() RMA() RMA() RMA() RMA() RMA() RMA() count -= 16;
    }
    dummy = seed;
#undef RMA
}
#pragma GCC diagnostic pop

static double latency_measure(char *buf, int nbits, int count) {
    double t_noaccess = 0, t_before, t_after, t, xs1 = 0, xs2 = 0, min_t = 0;
    double xs;
    int n;

    for (n = 1; n <= MAXREPEATS; n++) {
        t_before = gettime();
        random_read_test(buf, count, 1);
        t_after = gettime();
        if (n == 1 || t_after - t_before < t_noaccess)
            t_noaccess = t_after - t_before;
    }
    for (n = 1; n <= MAXREPEATS; n++) {
        t_before = gettime();
        random_read_test(buf, count, nbits);
        t_after = gettime();
        t = t_after - t_before - t_noaccess;
        if (t < 0)
            t = 0;
        xs1 += t;
        xs2 += t * t;
        if (n == 1 || t < min_t)
            min_t = t;
        if (n > 2) {
            xs = sqrt((xs2 * n - xs1 * xs1) / (n * (n - 1)));
            if (xs < min_t / 1000.)
                break;
        }
    }
    return min_t * 1000000000.0 / count;
}

static void latency_bench(double *l2_out, double *ram_out) {
    char *buf_alloc = malloc(0x2001000);
    char *buf = (char *)(((uintptr_t)buf_alloc + 4095) & ~(uintptr_t)4095);
    memset(buf, 0, 0x2000000);
    *l2_out = latency_measure(buf, 20, LATBENCH_COUNT);
    *ram_out = latency_measure(buf, 25, LATBENCH_COUNT);
    free(buf_alloc);
}

void bench_get_sysinfo(sysinfo_t *out) {
    memset(out, 0, sizeof(*out));
    out->threads = 3;
    out->is_4gb = (appletGetAppletType() == AppletType_Application);
    if (R_SUCCEEDED(clkrstInitialize())) {
        ClkrstSession s;
        clkrstOpenSession(&s, PcvModuleId_CpuBus, 3);
        clkrstGetClockRate(&s, &out->cpu_hz);
        clkrstCloseSession(&s);
        clkrstOpenSession(&s, PcvModuleId_GPU, 3);
        clkrstGetClockRate(&s, &out->gpu_hz);
        clkrstCloseSession(&s);
        clkrstOpenSession(&s, PcvModuleId_EMC, 3);
        clkrstGetClockRate(&s, &out->mem_hz);
        clkrstCloseSession(&s);
        clkrstExit();
    }
}

void bench_run_full(bench_results_t *out, bench_progress_fn progress, void *user) {
    const int threads = 3;
    const int size = SIZE;
    bool is_4gb = (appletGetAppletType() == AppletType_Application);
    memset(out, 0, sizeof(*out));

#define STEP(label, frac)                    \
    do {                                     \
        if (progress)                        \
            progress((label), (frac), user); \
    } while (0)

    STEP("GPU bandwidth", 0.05f);
    gpu_bw_run(is_4gb, &out->gpu_copy, &out->gpu_read, &out->gpu_write);

    int64_t *srcbuf, *dstbuf;
    void *poolbuf = alloc_nonaliased_buffers((void **)&srcbuf, size * threads, (void **)&dstbuf, size * threads, NULL, 0);

    STEP("CPU copy", 0.40f);
    out->cpu_copy = bandwidth_bench_helper(threads, dstbuf, srcbuf, size, aligned_block_copy);
    STEP("CPU read", 0.55f);
    out->cpu_read = bandwidth_bench_helper(threads, dstbuf, srcbuf, size, aligned_block_fetch);
    STEP("CPU write", 0.70f);
    out->cpu_write = bandwidth_bench_helper(threads, dstbuf, srcbuf, size, aligned_block_fill);
    free(poolbuf);

    STEP("Latency", 0.85f);
    latency_bench(&out->l2_ns, &out->ram_ns);

    STEP("Done", 1.0f);
#undef STEP
}

struct bench_ctx {
    int phase;
    bool is_4gb;
    void *pool;
    int64_t *src;
    int64_t *dst;
};

bench_ctx *bench_begin(void) {
    bench_ctx *c = (bench_ctx *)calloc(1, sizeof(bench_ctx));
    if (!c)
        return NULL;
    c->is_4gb = (appletGetAppletType() == AppletType_Application);
    c->pool = alloc_nonaliased_buffers((void **)&c->src, SIZE * 3, (void **)&c->dst, SIZE * 3, NULL, 0);
    return c;
}

bool bench_step(bench_ctx *c, bench_results_t *out, const char **label, float *frac) {
    const int threads = 3;
    const int size = SIZE;
    switch (c->phase) {
        case 0:
            gpu_bw_run(c->is_4gb, &out->gpu_copy, &out->gpu_read, &out->gpu_write);
            *label = "GPU bandwidth";
            *frac = 0.25f;
            break;
        case 1:
            out->cpu_copy = bandwidth_bench_helper(threads, c->dst, c->src, size, aligned_block_copy);
            *label = "CPU copy";
            *frac = 0.45f;
            break;
        case 2:
            out->cpu_read = bandwidth_bench_helper(threads, c->dst, c->src, size, aligned_block_fetch);
            *label = "CPU read";
            *frac = 0.60f;
            break;
        case 3:
            out->cpu_write = bandwidth_bench_helper(threads, c->dst, c->src, size, aligned_block_fill);
            *label = "CPU write";
            *frac = 0.75f;
            break;
        case 4:
            if (c->pool) {
                free(c->pool);
                c->pool = NULL;
            }
            latency_bench(&out->l2_ns, &out->ram_ns);
            *label = "Latency";
            *frac = 0.95f;
            break;
        default:
            *label = "Done";
            *frac = 1.0f;
            return false;
    }
    c->phase++;
    return true;
}

void bench_end(bench_ctx *c) {
    if (!c)
        return;
    if (c->pool)
        free(c->pool);
    free(c);
}
