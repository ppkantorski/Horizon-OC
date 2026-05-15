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

#include <switch.h>
#include <fuse.h>
#include "board_fuse.hpp"
#include <cstring>

namespace board {

    void SetGpuBracket(u16 speedo, u8 &gpuBracket) {
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

    void ReadFuses(FuseData &speedo, u64 fuseVa) {
        constexpr u32 FuseOffset = 0x800;
        u8 *fusePtr = reinterpret_cast<u8 *>(fuseVa) + FuseOffset;

        speedo.cpuSpeedo = *reinterpret_cast<u16 *>(fusePtr + FUSE_CPU_SPEEDO_0_CALIB);
        speedo.gpuSpeedo = *reinterpret_cast<u16 *>(fusePtr + FUSE_CPU_SPEEDO_2_CALIB);
        speedo.socSpeedo = *reinterpret_cast<u16 *>(fusePtr + FUSE_SOC_SPEEDO_0_CALIB);
        speedo.cpuIDDQ   = *reinterpret_cast<u16 *>(fusePtr + FUSE_CPU_IDDQ_CALIB) * 4;
        speedo.gpuIDDQ   = *reinterpret_cast<u16 *>(fusePtr + FUSE_GPU_IDDQ_CALIB) * 5;
        speedo.socIDDQ   = *reinterpret_cast<u16 *>(fusePtr + FUSE_SOC_IDDQ_CALIB) * 4;
        speedo.waferX    = *reinterpret_cast<s16 *>(fusePtr + FUSE_OPT_X_COORDINATE);
        speedo.waferY    = *reinterpret_cast<s16 *>(fusePtr + FUSE_OPT_Y_COORDINATE);
        speedo.waferX    = (speedo.waferX & BIT(8)) ? (speedo.waferX - 512) : speedo.waferX;
    }

}
