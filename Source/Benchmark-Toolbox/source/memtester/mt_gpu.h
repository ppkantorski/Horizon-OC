#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t loop;
    uint64_t mismatches;
    uint64_t size_mb;
    int running;
    int error;
    const char *status;
} mt_gpu_status_t;

void mt_gpu_start(int full);
void mt_gpu_stop(void);
int mt_gpu_running(void);
void mt_gpu_get(mt_gpu_status_t *out);

#ifdef __cplusplus
}
#endif
