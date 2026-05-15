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

#pragma once

#include <switch.h>
#include <hocclk.h>

namespace tsensor {

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

    struct TSensorTemps {
        s32 cpu;
        s32 gpu;
        s32 mem;
        s32 pllx;
    };

    void InitializeSoctherm();
    void ReadTSensors(TSensorTemps &temps);

}
