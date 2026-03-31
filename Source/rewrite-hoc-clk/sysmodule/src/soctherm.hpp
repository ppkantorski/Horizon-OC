/*
 * Copyright (c) 2014 - 2019, NVIDIA CORPORATION.  All rights reserved.
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

/* --------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <p-sam@d3vs.net>, <natinusala@gmail.com>, <m4x@m4xw.net>
 * wrote this file. As long as you retain this notice you can do whatever you
 * want with this stuff. If you meet any of us some day, and you think this
 * stuff is worth it, you can buy us a beer in return.  - The sys-clk authors
 * --------------------------------------------------------------------------
 */

#pragma once

#include <switch.h>
#include <sysclk.h>

namespace soctherm {

    #define R_UNLESS(rc)    \
    do {                    \
        if (R_FAILED(rc)) { \
            return;         \
        }                   \
    } while (0)

    struct TSensorConfig {
        u32 tall;
        u32 tiddq_en;
        u32 ten_count;
        u32 pdiv;
        u32 pdiv_ate;
        u32 tsample;
        u32 tsample_ate;
    };

    struct FuseCorrCoeff {
        s32 alpha;
        s32 beta;
    };

    struct TSensorGroup {
        const char *name;
        u8 id;
        u16 sensor_temp_offset;
        u32 sensor_temp_mask;
        u32 pdiv_mask;
        u32 pllx_hotspot_diff;
        u32 pllx_hotspot_mask;
        u32 hw_pllx_offset_mask;
        u32 hw_pllx_offset_en_mask;
        u32 thermtrip_enable_mask;
        u32 thermtrip_any_en_mask;
        u32 thermtrip_threshold_mask;
        u16 thermctl_lvl0_offset;
        u32 thermctl_isr_mask;
        u32 thermctl_lvl0_up_thresh_mask;
        u32 thermctl_lvl0_dn_thresh_mask;
    };

    struct TSensorGroupOffsets {
        u32 max;
        u32 min;
        u32 hw_offsetting_en;
        const TSensorGroup *ttg;
    };

    struct TSensor {
        const char *name;
        const u32 base;
        const TSensorConfig *config;
        const u32 calib_fuse_offset;
        const FuseCorrCoeff fuse_corr;
        const TSensorGroup *group;
    };

    struct TSensorFuse {
        u32 fuse_base_cp_mask;
        u32 fuse_base_cp_shift;
        u32 fuse_base_ft_mask;
        u32 fuse_base_ft_shift;
        u32 fuse_shift_ft_mask;
        u32 fuse_shift_ft_shift;
        u32 fuse_spare_realignment;
    };

    struct TSensorSharedCalib {
        u32 base_cp;
        u32 base_ft;
        u32 actual_temp_cp;
        u32 actual_temp_ft;
    };

    enum SocthermTSensor : u32 {
        SocthermTSensor_CPU0    = 0,
        SocthermTSensor_CPU1    = 1,
        SocthermTSensor_CPU2    = 2,
        SocthermTSensor_CPU3    = 3,
        SocthermTSensor_GPU     = 4,
        SocthermTSensor_PLLX    = 5,
        SocthermTSensor_MEM0    = 6,
        SocthermTSensor_MEM1    = 7,
        SocthermTSensor_EnumMax = 8,
    };

    void Initialize();
    void ReadSensors(TSensorTemps &temps);

}
