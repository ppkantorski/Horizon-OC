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
#include <fuse.h>
#include "board_fuse.hpp"
#include <cstring>

namespace board {

    void SetGpuBracket(u8 speedo, u8 &gpuBracket) {
        if (speedo <= 1624) {
            gpuBracket = 0;
            return;
        }

        if (speedo <= 1689) {
            gpuBracket = 1;
            return;
        }

        if (speedo <= 1753) {
            gpuBracket = 2;
            return;
        }

        /* >= 1754 */
        gpuBracket = 3;
    }

    void ReadFuses(FuseData &speedo) {
        u64 pid = 0;
        constexpr u64 UsbID = 0x0100000000000006;
        if (R_FAILED(pmdmntGetProcessId(&pid, UsbID))) {
            return;
        }

        Handle debug;
        if (R_FAILED(svcDebugActiveProcess(&debug, pid))) {
            return;
        }

        MemoryInfo mem_info = {};
        u32 pageinfo = 0;
        u64 addr = 0;

        u8 stack[0x10] = {};
        const u8 compare[0x10] = {};
        u8 dump[0x400] = {};
        constexpr u64 PageSize = 0x1000;

        while (true) {
            if (R_FAILED(svcQueryDebugProcessMemory(&mem_info, &pageinfo, debug, addr)) || mem_info.addr < addr) {
                break;
            }

            if (mem_info.type == MemType_Io && mem_info.size == PageSize) {
                if (R_FAILED(svcReadDebugProcessMemory(stack, debug, mem_info.addr, sizeof(stack)))) {
                    break;
                }

                if (memcmp(stack, compare, sizeof(stack)) == 0) {
                    if (R_FAILED(svcReadDebugProcessMemory(dump, debug, mem_info.addr + 0x800, sizeof(dump)))) {
                        break;
                    }

                    speedo.cpuSpeedo = *reinterpret_cast<u16*>(dump + FUSE_CPU_SPEEDO_0_CALIB);
                    speedo.gpuSpeedo = *reinterpret_cast<u16*>(dump + FUSE_CPU_SPEEDO_2_CALIB);
                    speedo.socSpeedo = *reinterpret_cast<u16*>(dump + FUSE_SOC_SPEEDO_0_CALIB);

                    speedo.cpuIDDQ   = *reinterpret_cast<u16*>(dump + FUSE_CPU_IDDQ_CALIB) * 4;
                    speedo.gpuIDDQ   = *reinterpret_cast<u16*>(dump + FUSE_GPU_IDDQ_CALIB) * 5;
                    speedo.socIDDQ   = *reinterpret_cast<u16*>(dump + FUSE_SOC_IDDQ_CALIB) * 4;
                    speedo.waferX    = *reinterpret_cast<u16*>(dump + FUSE_OPT_X_COORDINATE);
                    speedo.waferY    = *reinterpret_cast<u16*>(dump + FUSE_OPT_Y_COORDINATE);

                    svcCloseHandle(debug);
                    return;
                }
            }

            addr = mem_info.addr + mem_info.size;
        }

        svcCloseHandle(debug);
    }

}
