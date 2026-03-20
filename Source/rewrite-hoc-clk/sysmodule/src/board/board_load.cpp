/*
 * Copyright (c) Souldbminer, Lightos_ and Horizon OC Contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/* --------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <p-sam@d3vs.net>, <natinusala@gmail.com>, <m4x@m4xw.net>
 * wrote this file. As long as you retain this notice you can do whatever you
 * want with this stuff. If you meet any of us some day, and you think this
 * stuff is worth it, you can buy us a beer in return.  - The sys-clk authors
 * --------------------------------------------------------------------------
 */

#include <switch.h>

namespace board {

    Thread gpuThread;
    u32 gpuLoad;
    u32 _fd;

    void GpuLoadThread(Result *nvCheck) {
        constexpr u32 GpuSamples = 8;
        u32 gpu_load_array[GpuSamples] = {};
        size_t i = 0;
        constexpr u32 NVGPU_GPU_IOCTL_PMU_GET_GPU_LOAD  = 0x80044715;

        if (R_SUCCEEDED(nvCheck)) do {
            u32 temp;
            if (R_SUCCEEDED(nvIoctl(_fd, NVGPU_GPU_IOCTL_PMU_GET_GPU_LOAD, &temp))) {
                gpu_load_array[i++ % gpu_samples_average] = temp;
                gpuLoad = std::accumulate(&gpu_load_array[0], &gpu_load_array[gpu_samples_average], 0) / gpu_samples_average;
            }
            svcSleepThread(16'666'000); // wait a bit (this is the perfect amount of time to keep the reading accurate)
        } while(true);
    }

    void StartGpuLoad(Result nvCheck, u32 fd) {
        _fd = fd;

        threadCreate(&gpuThread, GpuLoadThread, &nvCheck, NULL, 0x1000, 0x3F, -2);
        threadStart(&gpuThread);
    }

    void ExitLoad() {
        threadClose(gpuThread);
    }

    void StartCpuLoad() {

    }

}
