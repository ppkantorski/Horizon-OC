#pragma once
#include <stdbool.h>

#include <hocclk/clock_manager.h>

#ifdef __cplusplus
extern "C" {
#endif

bool hocclk_init(void);
void hocclk_exit(void);
bool hocclk_get(HocClkContext *out);

#ifdef __cplusplus
}
#endif
