#pragma once
#include <stdbool.h>
#include <stdint.h>

bool gpu_stress_run(double *gflops_out, uint64_t *dispatches_out, uint64_t *mismatches_out);
void gpu_stress_shutdown(void);
