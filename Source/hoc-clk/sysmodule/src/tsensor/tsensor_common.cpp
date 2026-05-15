/*
 * Copyright (c) 2014 - 2019, NVIDIA CORPORATION.  All rights reserved.
 *
 * Copyright (c) 1994, Linus Torvalds
 *
 * Author:
 *	Mikko Perttunen <mperttunen@nvidia.com>
 *
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
#include "tsensor_common.hpp"

namespace tsensor {

    static s64 div64_s64(s64 dividend, s64 divisor) {
        return dividend / divisor;
    }

    static s64 div64_s64_precise(s64 a, s32 b) {
        s64 r, al;

        al = a << 16;

        r = div64_s64(al * 2 + 1, 2 * b);
        return r >> 16;
    }

    static s32 sign_extend32(u32 value, int index) {
        u8 shift = 31 - index;
        return (s32) (value << shift) >> shift;
    }

    void CalcSharedCal(const TSensorFuse *tfuse, TSensorSharedCalib *shared, u64 fuseVa) {
        constexpr u32 NominalCalibFt = 105;
        constexpr u32 NominalCalibCp = 25;

        s32 shifted_cp, shifted_ft;

        u32 val = ReadReg(fuseVa, FUSE_TSENSOR_COMMON);

        shared->base_cp = (val & tfuse->fuse_base_cp_mask) >> tfuse->fuse_base_cp_shift;
        shared->base_ft = (val & tfuse->fuse_base_ft_mask) >> tfuse->fuse_base_ft_shift;

        shifted_ft = (val & tfuse->fuse_shift_ft_mask) >> tfuse->fuse_shift_ft_shift;
        shifted_ft = sign_extend32(shifted_ft, 4);

        if (tfuse->fuse_spare_realignment) {
            val = ReadReg(fuseVa, tfuse->fuse_spare_realignment + FUSE_OFFSET);
        }

        shifted_cp = sign_extend32(val, 5);

        shared->actual_temp_cp = 2 * NominalCalibCp + shifted_cp;
        shared->actual_temp_ft = 2 * NominalCalibFt + shifted_ft;
    }

    void CalcTSensorCalib(const TSensorConfig *cfg, TSensorSharedCalib *shared, const FuseCorrCoeff *corr, u32 *calibration, u32 offset, u64 fuseVa) {
        #define FUSE_TSENSOR_CALIB_FT_TS_BASE_SHIFT	  13
        #define FUSE_TSENSOR_CALIB_FT_TS_BASE_MASK (0x1fff << 13)
        #define CALIB_COEFFICIENT                     1000000LL
        #define SENSOR_CONFIG2_THERMA_SHIFT 16
        #define SENSOR_CONFIG2_THERMB_SHIFT 0

        u32 val, calib;
        s32 actual_tsensor_ft, actual_tsensor_cp;
        s32 delta_sens, delta_temp;
        s32 mult, div;
        s16 therma, thermb;
        s64 temp;

        val = ReadReg(fuseVa, offset + FUSE_OFFSET);

        actual_tsensor_cp = (shared->base_cp * 64) + sign_extend32(val, 12);
        val = (val & FUSE_TSENSOR_CALIB_FT_TS_BASE_MASK) >> FUSE_TSENSOR_CALIB_FT_TS_BASE_SHIFT;
        actual_tsensor_ft = (shared->base_ft * 32) + sign_extend32(val, 12);

        delta_sens = actual_tsensor_ft - actual_tsensor_cp;
        delta_temp = shared->actual_temp_ft - shared->actual_temp_cp;

        mult = cfg->pdiv * cfg->tsample_ate;
        div = cfg->tsample * cfg->pdiv_ate;

        temp = (s64)delta_temp * (1LL << 13) * mult;
        therma = div64_s64_precise(temp, (s64)delta_sens * div);

        temp = ((s64)actual_tsensor_ft * shared->actual_temp_cp) - ((s64)actual_tsensor_cp * shared->actual_temp_ft);
        thermb = div64_s64_precise(temp, delta_sens);

        temp = (s64)therma * corr->alpha;
        therma = div64_s64_precise(temp, CALIB_COEFFICIENT);

        temp = (s64)thermb * corr->alpha + corr->beta;
        thermb = div64_s64_precise(temp, CALIB_COEFFICIENT);

        calib = ((u16)therma << SENSOR_CONFIG2_THERMA_SHIFT) | ((u16)thermb << SENSOR_CONFIG2_THERMB_SHIFT);

        *calibration = calib;
    }

}