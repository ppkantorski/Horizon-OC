#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t iters;
    uint64_t mismatches;
    int threads;
    int running;
} cpu_stress_status_t;

void cpu_stress_start(int mode);
void cpu_stress_stop(void);
int cpu_stress_running(void);
void cpu_stress_get(cpu_stress_status_t *out);

#ifdef __cplusplus
}
#endif
