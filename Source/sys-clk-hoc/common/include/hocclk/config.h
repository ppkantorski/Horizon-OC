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
#include "board.h"
typedef enum {
    HocClkConfigValue_PollingIntervalMs = 0,
    HocClkConfigValue_TempLogIntervalMs,
    HocClkConfigValue_FreqLogIntervalMs,
    HocClkConfigValue_PowerLogIntervalMs,
    HocClkConfigValue_CsvWriteIntervalMs,

    HocClkConfigValue_UncappedClocks,
    HocClkConfigValue_OverwriteBoostMode,
    HocClkConfigValue_ReverseNXSync,

    HocClkConfigValue_EristaMaxCpuClock,
    HocClkConfigValue_MarikoMaxCpuClock,

    HocClkConfigValue_ThermalThrottle,
    HocClkConfigValue_ThermalThrottleThreshold,

    HocClkConfigValue_HandheldTDP,
    HocClkConfigValue_HandheldTDPLimit,

    HocClkConfigValue_LiteTDPLimit,

    HocClkConfigValue_BatteryChargeCurrent,

    HocClkConfigValue_OverwriteRefreshRate,
    HocClkConfigValue_MaxDisplayClockH,

    HocClkConfigValue_DVFSMode,
    HocClkConfigValue_DVFSOffset,
    HocClkConfigValue_LiveCpuUv,
    HocClkConfigValue_EnableExperimentalSettings,

    HocClkConfigValue_GPUScheduling,
    HocClkConfigValue_GPUSchedulingMethod,

    HocClkConfigValue_RAMVoltDisplayMode,
    HocClkConfigValue_CpuGovernorMinimumFreq,
    HocClkConfigValue_DisplayVoltage,

    HocClkConfigValue_MemoryFrequencyMeasurementMode,
    HocClkConfigValue_RamDisplayUnit,
    HocClkConfigValue_IsFirstLoad,
    // The overlay writes "allow_governing" to [values]; if this enum value
    // [system] instead and never find it — defaulting to 0 always.
    HocClkConfigValue_AllowGoverning,  // HOC: enable per-profile governor (0=off, 1=on)

    // ------------------------------------------------------------------
    // KIP-derived hardware values (read-only, memory-only).
    //
    // Populated directly from hoc.kip at boot; kept in memory only - never
    // written to config.ini and never sent over IPC. The KIP is the single
    // source of truth (managed externally by the HOC Toolkit). Only the
    // values sys-clk-hoc actually consumes at runtime are mirrored here;
    // everything else is read straight from the KIP by the overlay/toolkit.
    // by config.cpp (never persisted, preserved across config reloads).
    // ------------------------------------------------------------------
    KipConfigValue_eristaCpuUV,
    KipConfigValue_marikoCpuUVLow,
    KipConfigValue_marikoCpuUVHigh,
    KipConfigValue_tableConf,
    KipConfigValue_marikoGpuUV,

    HocClkConfigValue_EnumMax,
} HocClkConfigValue;

// First KIP-derived (memory-only) config value. Everything at this index or
// above is populated from hoc.kip at boot and never persisted to config.ini.
#define KipConfigValue_FIRST KipConfigValue_eristaCpuUV

typedef struct {
    uint64_t values[HocClkConfigValue_EnumMax];
} HocClkConfigValueList;

static inline const char* hocclkFormatConfigValue(HocClkConfigValue val, bool pretty)
{
    switch(val)
    {
        case HocClkConfigValue_PollingIntervalMs:
            return pretty ? "Polling Interval (ms)" : "poll_interval_ms";
        case HocClkConfigValue_TempLogIntervalMs:
            return pretty ? "Temperature logging interval (ms)" : "temp_log_interval_ms";
        case HocClkConfigValue_FreqLogIntervalMs:
            return pretty ? "Frequency logging interval (ms)" : "freq_log_interval_ms";
        case HocClkConfigValue_PowerLogIntervalMs:
            return pretty ? "Power logging interval (ms)" : "power_log_interval_ms";
        case HocClkConfigValue_CsvWriteIntervalMs:
            return pretty ? "CSV write interval (ms)" : "csv_write_interval_ms";

        case HocClkConfigValue_UncappedClocks:
            return pretty ? "Uncapped Clocks" : "uncapped_clocks";
        case HocClkConfigValue_OverwriteBoostMode:
            return pretty ? "Overwrite Boost Mode" : "ow_boost";
        case HocClkConfigValue_ReverseNXSync:
            return pretty ? "Sync ReverseNX" : "reversenx_sync";

        case HocClkConfigValue_EristaMaxCpuClock:
            return pretty ? "CPU Max Clock" : "cpu_max_e";

        case HocClkConfigValue_MarikoMaxCpuClock:
            return pretty ? "CPU Max Display Clock" : "cpu_max_m";

        case HocClkConfigValue_ThermalThrottle:
            return pretty ? "Thermal Throttle" : "thermal_throttle";

        case HocClkConfigValue_ThermalThrottleThreshold:
            return pretty ? "Thermal Throttle Threshold" : "thermal_throttle_threshold";

        case HocClkConfigValue_HandheldTDP:
            return pretty ? "Handheld TDP" : "handheld_tdp";

        case HocClkConfigValue_HandheldTDPLimit:
            return pretty ? "Handheld TDP Limit" : "tdp_limit";

        case HocClkConfigValue_LiteTDPLimit:
            return pretty ? "Handheld TDP Limit" : "tdp_limit_l";

        case HocClkConfigValue_BatteryChargeCurrent:
            return pretty ? "Battery Charge Current" : "bat_charge_current";

        case HocClkConfigValue_OverwriteRefreshRate:
            return pretty ? "Display Refresh Rate Changing" : "drr_changing";

        case HocClkConfigValue_MaxDisplayClockH:
            return pretty ? "Max Display Clock (Handheld)" : "drr_max_clock";

        case HocClkConfigValue_DVFSMode:
            return pretty ? "DVFS Mode" : "dvfs_mode";

        case HocClkConfigValue_DVFSOffset:
            return pretty ? "DVFS Offset" : "dvfs_offset";

        case HocClkConfigValue_GPUScheduling:
            return pretty ? "GPU Scheduling" : "gpu_scheduling";

        case HocClkConfigValue_GPUSchedulingMethod:
            return pretty ? "GPU Scheduling Method" : "gpu_sched_method";

        case HocClkConfigValue_LiveCpuUv:
            return pretty ? "Live CPU Undervolt" : "live_cpu_uv";

        case HocClkConfigValue_AllowGoverning:
            return pretty ? "Allow Governing" : "allow_governing";
        case HocClkConfigValue_EnableExperimentalSettings:
            return pretty ? "Enable Experimental Settings" : "enable_experimental_settings";

        case HocClkConfigValue_RAMVoltDisplayMode:
            return pretty ? "RAM Voltage / Usage Display Mode" : "ram_volt_usage_display_mode";
        case HocClkConfigValue_CpuGovernorMinimumFreq:
            return pretty ? "CPU Governor Minimum Frequency" : "cpu_gov_min_freq";

        case HocClkConfigValue_DisplayVoltage:
            return pretty ? "Display Voltage" : "display_voltage";

        case HocClkConfigValue_MemoryFrequencyMeasurementMode:
            return pretty ? "RAM Frequency Measurement Mode" : "mem_freq_measurement_mode";

        case HocClkConfigValue_RamDisplayUnit:
            return pretty ? "RAM Frequency Display Unit" : "RAM_display_unit";

        // KIP-derived values (read-only; only the values used at runtime)
        case KipConfigValue_eristaCpuUV:
            return pretty ? "Erista CPU Undervolt" : "erista_cpu_uv";
        case KipConfigValue_marikoCpuUVLow:
            return pretty ? "Mariko CPU Undervolt (Low)" : "mariko_cpu_uv_low";
        case KipConfigValue_marikoCpuUVHigh:
            return pretty ? "Mariko CPU Undervolt (High)" : "mariko_cpu_uv_high";
        case KipConfigValue_tableConf:
            return pretty ? "Table Config" : "kip_table_conf";
        case KipConfigValue_marikoGpuUV:
            return pretty ? "Mariko GPU Undervolt" : "mariko_gpu_uv";
        case HocClkConfigValue_IsFirstLoad:
            return pretty ? "Is First Load" : "is_first_load";
        default:
            return pretty ? "[cfg] no enum format string" : "err_no_format_string";
    }
}

static inline uint64_t hocclkDefaultConfigValue(HocClkConfigValue val)
{
    switch(val)
    {
        case HocClkConfigValue_PollingIntervalMs:
            return 300ULL;
        case HocClkConfigValue_TempLogIntervalMs:
        case HocClkConfigValue_FreqLogIntervalMs:
        case HocClkConfigValue_PowerLogIntervalMs:
        case HocClkConfigValue_CsvWriteIntervalMs:
        case HocClkConfigValue_UncappedClocks:
        case HocClkConfigValue_OverwriteBoostMode:
        case HocClkConfigValue_ReverseNXSync:
        case HocClkConfigValue_BatteryChargeCurrent:
        case HocClkConfigValue_OverwriteRefreshRate:
        case HocClkConfigValue_GPUScheduling:
        case HocClkConfigValue_LiveCpuUv:
        case HocClkConfigValue_GPUSchedulingMethod:
        case HocClkConfigValue_MemoryFrequencyMeasurementMode:
            return 0ULL;
        case HocClkConfigValue_RamDisplayUnit:
            return (uint64_t)RamDisplayUnit_MHz;
        case HocClkConfigValue_EristaMaxCpuClock:
            return 1785ULL;

        case HocClkConfigValue_MarikoMaxCpuClock:
            return 1963ULL;

        case HocClkConfigValue_ThermalThrottle:
        case HocClkConfigValue_IsFirstLoad:
        case HocClkConfigValue_DVFSMode:
            return 1ULL;
        case HocClkConfigValue_HandheldTDP:
            return 0ULL;
        case HocClkConfigValue_ThermalThrottleThreshold:
            return 70ULL;
        case HocClkConfigValue_HandheldTDPLimit:
            return 9600ULL; // 8600mW will trigger on erista stock, so raise it a bit
        case HocClkConfigValue_LiteTDPLimit:
            return 6400ULL; // 0.5C
        case HocClkConfigValue_CpuGovernorMinimumFreq:
            return 612000000ULL; // 612MHz
        case HocClkConfigValue_MaxDisplayClockH:
            return 60ULL;
        case HocClkConfigValue_DisplayVoltage:
            return 1200ULL; // Auto
        default:
            return 0ULL;
    }
}

static inline uint64_t hocclkValidConfigValue(HocClkConfigValue val, uint64_t input)
{
    switch(val)
    {
        case HocClkConfigValue_EristaMaxCpuClock:
        case HocClkConfigValue_MarikoMaxCpuClock:
        case HocClkConfigValue_ThermalThrottleThreshold:
        case HocClkConfigValue_HandheldTDPLimit:
        case HocClkConfigValue_LiteTDPLimit:
        case HocClkConfigValue_PollingIntervalMs:
        case HocClkConfigValue_MaxDisplayClockH:
            return input > 0;

        case HocClkConfigValue_TempLogIntervalMs:
        case HocClkConfigValue_FreqLogIntervalMs:
        case HocClkConfigValue_PowerLogIntervalMs:
        case HocClkConfigValue_CsvWriteIntervalMs:
        case HocClkConfigValue_UncappedClocks:
        case HocClkConfigValue_OverwriteBoostMode:
        case HocClkConfigValue_ReverseNXSync:
        case HocClkConfigValue_ThermalThrottle:
        case HocClkConfigValue_HandheldTDP:
        case HocClkConfigValue_OverwriteRefreshRate:
        case HocClkConfigValue_IsFirstLoad:
        case HocClkConfigValue_AllowGoverning:
        case HocClkConfigValue_EnableExperimentalSettings:
        case HocClkConfigValue_LiveCpuUv:
        case HocClkConfigValue_GPUSchedulingMethod:
            return (input & 0x1) == input;

        case KipConfigValue_eristaCpuUV:
        case KipConfigValue_marikoCpuUVLow:
        case KipConfigValue_marikoCpuUVHigh:
        case KipConfigValue_tableConf:
        case KipConfigValue_marikoGpuUV:
        case HocClkConfigValue_DVFSMode:
        case HocClkConfigValue_DVFSOffset:
        case HocClkConfigValue_GPUScheduling:
        case HocClkConfigValue_RAMVoltDisplayMode:
        case HocClkConfigValue_CpuGovernorMinimumFreq:
        case HocClkConfigValue_MemoryFrequencyMeasurementMode:
        case HocClkConfigValue_RamDisplayUnit:
            return true;
        case HocClkConfigValue_BatteryChargeCurrent:
            return ((input >= 1024) && (input <= 3072)) || !input;
        case HocClkConfigValue_DisplayVoltage:
            return ((input >= 800) && (input <= 1325));

        default:
            return false;
    }
}