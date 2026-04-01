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


#pragma once

#include <stdint.h>
#include "board.h"

typedef struct
{
    uint64_t applicationId;
    SysClkProfile profile;
    uint32_t freqs[SysClkModule_EnumMax];
    uint32_t realFreqs[SysClkModule_EnumMax];
    uint32_t overrideFreqs[SysClkModule_EnumMax];
    uint32_t temps[SysClkThermalSensor_EnumMax];
    int32_t power[SysClkPowerSensor_EnumMax];
    uint32_t partLoad[SysClkPartLoad_EnumMax];
    uint32_t voltages[HocClkVoltage_EnumMax];
    u16 speedos[HorizonOCSpeedo_EnumMax];
    u16 iddq[HorizonOCSpeedo_EnumMax];
    u16 waferX;
    u16 waferY;

    // Misc stuff
    GpuSchedulingMode gpuSchedulingMode;
    bool isSysDockInstalled;
    bool isSaltyNXInstalled;
    bool isUsingRetroSuper;
    u8 maxDisplayFreq;
    u8 dramID;
    bool isDram8GB;

    // FPS / Resolution
    u8 fps;
    u16 resolutionHeight;
} SysClkContext;

typedef struct
{
    union {
        uint32_t mhz[+SysClkProfile_EnumMax * +SysClkModule_EnumMax];
        uint32_t mhzMap[+SysClkProfile_EnumMax][+SysClkModule_EnumMax];
    };
} SysClkTitleProfileList;

#define SYSCLK_FREQ_LIST_MAX 32

#define GLOBAL_PROFILE_ID 0xA111111111111111