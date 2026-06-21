#pragma once
#include <stdbool.h>

bool gpu_bw_run(bool is_4gb, double *copy_out, double *read_out, double *write_out);
