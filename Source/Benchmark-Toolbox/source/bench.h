#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t cpu_hz;
    uint32_t gpu_hz;
    uint32_t mem_hz;
    bool is_4gb;
    int threads;
} sysinfo_t;

void bench_get_sysinfo(sysinfo_t *out);

typedef struct {
    double gpu_copy, gpu_read, gpu_write;
    double cpu_copy, cpu_read, cpu_write;
    double l2_ns, ram_ns;
} bench_results_t;

typedef void (*bench_progress_fn)(const char *stage, float frac, void *user);

void bench_run_full(bench_results_t *out, bench_progress_fn progress, void *user);

typedef struct bench_ctx bench_ctx;
bench_ctx *bench_begin(void);
bool bench_step(bench_ctx *ctx, bench_results_t *out, const char **label, float *frac);
void bench_end(bench_ctx *ctx);

#ifdef __cplusplus
}
#endif
