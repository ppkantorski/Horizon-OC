#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void run_furmark_start(int which);
void run_furmark_stop(void);
int run_furmark_running(void);
float run_furmark_fps(void);
float run_furmark_cpu_fps(void);

#ifdef __cplusplus
}
#endif
