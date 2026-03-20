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
#include <stddef.h>
#include <stdbool.h>
#include <switch/types.h>
typedef enum
{
    SysClkSocType_Erista = 0,
    SysClkSocType_Mariko,
    SysClkSocType_EnumMax
} SysClkSocType;

typedef enum
{
    HorizonOCConsoleType_Icosa = 0,
    HorizonOCConsoleType_Copper,
    HorizonOCConsoleType_Hoag,
    HorizonOCConsoleType_Iowa,
    HorizonOCConsoleType_Calcio,
    HorizonOCConsoleType_Aula,
    HorizonOCConsoleType_EnumMax,
} HorizonOCConsoleType;

typedef enum {
    HocClkVoltage_SOC = 0,
    HocClkVoltage_EMCVDD2,
    HocClkVoltage_CPU,
    HocClkVoltage_GPU,
    HocClkVoltage_EMCVDDQ_MarikoOnly,
    HocClkVoltage_Display,
    HocClkVoltage_Battery,
    HocClkVoltage_EnumMax,
} HocClkVoltage;

typedef enum
{
    SysClkProfile_Handheld = 0,
    SysClkProfile_HandheldCharging,
    SysClkProfile_HandheldChargingUSB,
    SysClkProfile_HandheldChargingOfficial,
    SysClkProfile_Docked,
    SysClkProfile_EnumMax
} SysClkProfile;

typedef enum
{
    SysClkModule_CPU = 0,
    SysClkModule_GPU,
    SysClkModule_MEM,
    HorizonOCModule_Governor,
    HorizonOCModule_Display,
    SysClkModule_EnumMax,
} SysClkModule;

typedef enum
{
    SysClkThermalSensor_SOC = 0,
    SysClkThermalSensor_PCB,
    SysClkThermalSensor_Skin,
    HorizonOCThermalSensor_Battery,
    HorizonOCThermalSensor_PMIC,
    SysClkThermalSensor_EnumMax
} SysClkThermalSensor;

typedef enum
{
    SysClkPowerSensor_Now = 0,
    SysClkPowerSensor_Avg,
    SysClkPowerSensor_EnumMax
} SysClkPowerSensor;

typedef enum
{
    SysClkPartLoad_EMC = 0,
    SysClkPartLoad_EMCCpu,
    HocClkPartLoad_GPU,
    HocClkPartLoad_CPUMax,
    HocClkPartLoad_BAT,
    HocClkPartLoad_FAN,
    SysClkPartLoad_EnumMax
} SysClkPartLoad;

typedef enum {
    HorizonOCSpeedo_CPU = 0,
    HorizonOCSpeedo_GPU,
    HorizonOCSpeedo_SOC,
    HorizonOCSpeedo_EnumMax,
} HorizonOCSpeedo;

typedef enum {
    GPUUVLevel_NoUV = 0,
    GPUUVLevel_SLT,
    GPUUVLevel_HiOPT,
    GPUUVLevel_EnumMax,
} GPUUndervoltLevel;

enum {
    DVFSMode_Disabled = 0,
    DVFSMode_Hijack,
    // DVFSMode_OfficialService,
    // DVFSMode_Hack,
    DVFSMode_EnumMax,
};

typedef enum {
    GpuSchedulingMode_DoNotOverride = 0,
    GpuSchedulingMode_Enabled,
    GpuSchedulingMode_Disabled,
    GpuSchedulingMode_EnumMax,
} GpuSchedulingMode;

typedef enum {
    GpuSchedulingOverrideMethod_Ini = 0,
    GpuSchedulingOverrideMethod_NvService,
    GpuSchedulingOverrideMethod_EnumMax,
} GpuSchedulingOverrideMethod;
typedef enum {
    ComponentGovernor_DoNotOverride = 0,
    ComponentGovernor_Enabled       = 1,
    ComponentGovernor_Disabled      = 2,
    ComponentGovernor_EnumMax,
} ComponentGovernorState;
typedef enum {
    RamDisplayMode_VDD2VDDQ = 0,
    RamDisplayMode_VDD2Usage,
    RamDisplayMode_VDDQUsage,
    RamDisplayMode_EnumMax,
} RamDisplayMode;

#define SYSCLK_ENUM_VALID(n, v) ((v) < n##_EnumMax)

// Packed u32
// Bits 0-7 - CPU
// Bits 8-15 - GPU
// Bits 16-23 - VRR
// Bits 24-32 - unused

inline u32 GovernorStatePack(u8 cpu, u8 gpu, u8 vrr) {
    return (u32)cpu | ((u32)gpu << 8) | ((u32)vrr << 16);
}
inline u8 GovernorStateCpu(u32 p) {
    return (u8)(p         & 0xFF);
}
inline u8 GovernorStateGpu(u32 p) {
    return (u8)((p >>  8) & 0xFF);
}
inline u8 GovernorStateVrr(u32 p) {
    return (u8)((p >> 16) & 0xFF);
}

static inline const char* sysclkFormatModule(SysClkModule module, bool pretty)
{
    switch(module)
    {
        case SysClkModule_CPU:
            return pretty ? "CPU" : "cpu";
        case SysClkModule_GPU:
            return pretty ? "GPU" : "gpu";
        case SysClkModule_MEM:
            return pretty ? "Memory" : "mem";
        case HorizonOCModule_Display:
            return pretty ? "Display" : "display";
        case HorizonOCModule_Governor:
            return pretty ? "Governor" : "governor";
        default:
            return "null";
    }
}

static inline const char* sysclkFormatThermalSensor(SysClkThermalSensor thermSensor, bool pretty)
{
    switch(thermSensor)
    {
        case SysClkThermalSensor_SOC:
            return pretty ? "SOC" : "soc";
        case SysClkThermalSensor_PCB:
            return pretty ? "PCB" : "pcb";
        case SysClkThermalSensor_Skin:
            return pretty ? "Skin" : "skin";
        case HorizonOCThermalSensor_Battery:
            return pretty ? "BAT" : "battery";
        case HorizonOCThermalSensor_PMIC:
            return pretty ? "PMIC" : "pmic";

        default:
            return NULL;
    }
}

static inline const char* sysclkFormatPowerSensor(SysClkPowerSensor powSensor, bool pretty)
{
    switch(powSensor)
    {
        case SysClkPowerSensor_Now:
            return pretty ? "Now" : "now";
        case SysClkPowerSensor_Avg:
            return pretty ? "Avg" : "avg";
        default:
            return NULL;
    }
}

static inline const char* sysclkFormatProfile(SysClkProfile profile, bool pretty)
{
    switch(profile)
    {
        case SysClkProfile_Docked:
            return pretty ? "Docked" : "docked";
        case SysClkProfile_Handheld:
            return pretty ? "Handheld" : "handheld";
        case SysClkProfile_HandheldCharging:
            return pretty ? "Charging" : "handheld_charging";
        case SysClkProfile_HandheldChargingUSB:
            return pretty ? "USB Charger" : "handheld_charging_usb";
        case SysClkProfile_HandheldChargingOfficial:
            return pretty ? "PD Charger" : "handheld_charging_official";
        default:
            return NULL;
    }
}


static inline const char* hocClkFormatVoltage(HocClkVoltage voltage, bool pretty)
{
    switch(voltage)
    {
        case HocClkVoltage_CPU:
            return pretty ? "CPU" : "cpu";
        case HocClkVoltage_GPU:
            return pretty ? "GPU" : "gpu";
        case HocClkVoltage_EMCVDD2:
            return pretty ? "VDD2" : "emcvdd2";
        case HocClkVoltage_EMCVDDQ_MarikoOnly:
            return pretty ? "VDDQ" : "vddq";
        case HocClkVoltage_SOC:
            return pretty ? "SOC" : "soc";
        case HocClkVoltage_Display:
            return pretty ? "Display" : "display";
        default:
            return NULL;
    }
}