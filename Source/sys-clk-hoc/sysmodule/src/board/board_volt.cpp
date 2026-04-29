/*
 * Copyright (c) Souldbminer, Lightos_ and Horizon OC Contributors
 * 
 * Copyright (c) B3711
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
#include <hocclk.h>
#include <memmem.h>
#include <registers.h>
#include <cstring>
#include <rgltr.h>
#include <battery.h>
#include "board.hpp"
#include "board_freq.hpp"
#include "board_volt.hpp"
#include "../file_utils.hpp"

namespace board {

    GpuVoltData voltData = {};
    u32 cpuVoltTable[32] = {}; // 32LUT
    u64 cldvfs;
    CpuDfllData cachedTune;

    /* ... This really needs some cleanup... */
    namespace {
        struct EristaCpuUvEntry {
            u32 tune0;
            u32 tune1;
        };

        struct MarikoCpuUvEntry {
            u32 tune0_low;
            u32 tune0_high;
            u32 tune1_low;
            u32 tune1_high;
        };

        EristaCpuUvEntry eristaCpuUvTable[5] = {
            {0xffff, 0x27007ff},
            {0xefff, 0x27407ff},
            {0xdfff, 0x27807ff},
            {0xdfdf, 0x27a07ff},
            {0xcfdf, 0x37007ff},
        };

        MarikoCpuUvEntry marikoCpuUvLow[12] = {
            {0xffa0, 0xffff, 0x21107ff, 0},
            {0x0, 0xffdf, 0x21107ff, 0x27207ff},
            {0xffdf, 0xffdf, 0x21107ff, 0x27307ff},
            {0xffff, 0xffdf, 0x21107ff, 0x27407ff},
            {0x0, 0xffdf, 0x21607ff, 0x27707ff},
            {0x0, 0xffdf, 0x21607ff, 0x27807ff},
            {0x0, 0xdfff, 0x21607ff, 0x27b07ff},
            {0xdfff, 0xdfff, 0x21707ff, 0x27b07ff},
            {0xdfff, 0xdfff, 0x21707ff, 0x27c07ff},
            {0xdfff, 0xdfff, 0x21707ff, 0x27d07ff},
            {0xdfff, 0xdfff, 0x21707ff, 0x27e07ff},
            {0xdfff, 0xdfff, 0x21707ff, 0x27f07ff},
        };

        MarikoCpuUvEntry marikoCpuUvHigh[12] = {
            {0x0, 0xffff, 0, 0},
            {0x0, 0xffdf, 0, 0x27207ff},
            {0x0, 0xffdf, 0, 0x27307ff},
            {0x0, 0xffdf, 0, 0x27407ff},
            {0x0, 0xffdf, 0, 0x27707ff},
            {0x0, 0xffdf, 0, 0x27807ff},
            {0x0, 0xdfff, 0, 0x27b07ff},
            {0x0, 0xdfff, 0, 0x27c07ff},
            {0x0, 0xdfff, 0, 0x27d07ff},
            {0x0, 0xdfff, 0, 0x27e07ff},
            {0x0, 0xdfff, 0, 0x27f07ff},
            {0x0, 0xdfff, 0, 0x27f07ff},
        };
    }

    void CacheDfllData() {
        Result rc = QueryMemoryMapping(&cldvfs, CLDVFS_REGION_BASE, CLDVFS_REGION_SIZE);
        ASSERT_RESULT_OK(rc, "QueryMemoryMapping (cldvfs)");

        if (GetSocType() == HocClkSocType_Erista) {
            cachedTune.tune0Low = *reinterpret_cast<u32 *>(cldvfs + CL_DVFS_TUNE0_0);
            cachedTune.tune1Low = *reinterpret_cast<u32 *>(cldvfs + CL_DVFS_TUNE1_0);
        } else {
            SetHz(HocClkModule_CPU, 1785000000);
            cachedTune.tune0High = *reinterpret_cast<u32 *>(cldvfs + CL_DVFS_TUNE0_0);
            ResetToStockCpu();
        }
    }

    /* TODO: clean up this code. */
    void SetDfllTunings(u32 levelLow, u32 levelHigh, u32 tbreakPoint) {
        u32* tune0_ptr = reinterpret_cast<u32 *>(cldvfs + CL_DVFS_TUNE0_0);
        u32* tune1_ptr = reinterpret_cast<u32 *>(cldvfs + CL_DVFS_TUNE1_0);
        if (GetSocType() == HocClkSocType_Mariko) {
            if (GetHz(HocClkModule_CPU) < tbreakPoint && (levelLow || levelHigh)) {
                if (levelLow) {
                    *tune0_ptr = marikoCpuUvLow[levelLow-1].tune0_low;
                    *tune1_ptr = marikoCpuUvLow[levelLow-1].tune1_low;
                }
                return;
            } else {
                if (levelLow) {
                    *tune0_ptr = marikoCpuUvLow[levelLow-1].tune0_low;
                    *tune1_ptr = marikoCpuUvLow[levelLow-1].tune1_low;
                }
                if (levelHigh) {
                    *tune0_ptr = marikoCpuUvHigh[levelHigh-1].tune0_high;
                    *tune1_ptr = marikoCpuUvHigh[levelHigh-1].tune1_high;
                }
                return;
            }
            if (GetHz(HocClkModule_CPU) < tbreakPoint || (!levelLow)) { // account for tbreak
                *tune0_ptr = 0xCFFF;
                *tune1_ptr = 0xFF072201;
                return;
            } else if (GetHz(HocClkModule_CPU) >= tbreakPoint || (!levelHigh)) {
                *tune0_ptr = cachedTune.tune0High; // per console?
                *tune1_ptr = 0xFFF7FF3F;
                return;
            }
        } else {
            if (GetHz(HocClkModule_CPU) < tbreakPoint || (!levelLow)) { // account for tbreak
                *tune0_ptr = cachedTune.tune0Low; // I think each erista has a different tune0/tune1?
                *tune1_ptr = cachedTune.tune1Low;
                return;
            } else {
                if (levelLow) {
                    *tune0_ptr = eristaCpuUvTable[levelLow-1].tune0;
                    *tune1_ptr = eristaCpuUvTable[levelLow-1].tune1;
                } else {
                    *tune0_ptr = 0x0;
                    *tune1_ptr = 0x0;
                }
            }
        }
    }

    /*
    enum TableConfig: u32 {
        DEFAULT_TABLE = 1,
        TBREAK_1581 = 2,
        TBREAK_1683 = 3,
        EXTREME_TABLE = 4,
    };
    */
    u32 CalculateTbreak(u32 table) {
        if (GetSocType() == HocClkSocType_Erista) {
            return 1581000000;
        } else {
            switch (table) {
                case 1 ... 2:
                case 4:
                    return 1581000000;
                case 3:
                    return 1683000000;
                default:
                    return 1581000000;
            }
        }
    }

    /*
    * Switch Power domains (max77620):
    * Name  | Usage         | uV step | uV min | uV default | uV max  | Init
    *-------+---------------+---------+--------+------------+---------+------------------
    *  sd0  | SoC           | 12500   | 600000 |  625000    | 1400000 | 1.125V (pkg1.1)
    *  sd1  | SDRAM         | 12500   | 600000 | 1125000    | 1125000 | 1.1V   (pkg1.1)
    *  sd2  | ldo{0-1, 7-8} | 12500   | 600000 | 1325000    | 1350000 | 1.325V (pcv)
    *  sd3  | 1.8V general  | 12500   | 600000 | 1800000    | 1800000 |
    *  ldo0 | Display Panel | 25000   | 800000 | 1200000    | 1200000 | 1.2V   (pkg1.1)
    *  ldo1 | XUSB, PCIE    | 25000   | 800000 | 1050000    | 1050000 | 1.05V  (pcv)
    *  ldo2 | SDMMC1        | 50000   | 800000 | 1800000    | 3300000 |
    *  ldo3 | GC ASIC       | 50000   | 800000 | 3100000    | 3100000 | 3.1V   (pcv)
    *  ldo4 | RTC           | 12500   | 800000 |  850000    |  850000 | 0.85V  (AO, pcv)
    *  ldo5 | GC Card       | 50000   | 800000 | 1800000    | 1800000 | 1.8V   (pcv)
    *  ldo6 | Touch, ALS    | 50000   | 800000 | 2900000    | 2900000 | 2.9V   (pcv)
    *  ldo7 | XUSB          | 50000   | 800000 | 1050000    | 1050000 | 1.05V  (pcv)
    *  ldo8 | XUSB, DP, MCU | 50000   | 800000 | 1050000    | 2800000 | 1.05V/2.8V (pcv)

    typedef enum {
        PcvPowerDomainId_Max77620_Sd0  = 0x3A000080,
        PcvPowerDomainId_Max77620_Sd1  = 0x3A000081, // vdd2
        PcvPowerDomainId_Max77620_Sd2  = 0x3A000082,
        PcvPowerDomainId_Max77620_Sd3  = 0x3A000083,
        PcvPowerDomainId_Max77620_Ldo0 = 0x3A0000A0,
        PcvPowerDomainId_Max77620_Ldo1 = 0x3A0000A1,
        PcvPowerDomainId_Max77620_Ldo2 = 0x3A0000A2,
        PcvPowerDomainId_Max77620_Ldo3 = 0x3A0000A3,
        PcvPowerDomainId_Max77620_Ldo4 = 0x3A0000A4,
        PcvPowerDomainId_Max77620_Ldo5 = 0x3A0000A5,
        PcvPowerDomainId_Max77620_Ldo6 = 0x3A0000A6,
        PcvPowerDomainId_Max77620_Ldo7 = 0x3A0000A7,
        PcvPowerDomainId_Max77620_Ldo8 = 0x3A0000A8,
        PcvPowerDomainId_Max77621_Cpu  = 0x3A000003,
        PcvPowerDomainId_Max77621_Gpu  = 0x3A000004,
        PcvPowerDomainId_Max77812_Cpu  = 0x3A000003,
        PcvPowerDomainId_Max77812_Gpu  = 0x3A000004,
        PcvPowerDomainId_Max77812_Dram = 0x3A000005, // vddq
    } PowerDomainId;
    */
    u32 GetVoltage(HocClkVoltage voltage) {
        RgltrSession session;
        Result rc = 0;
        u32 out = 0;
        BatteryChargeInfo info;

        switch (voltage) {
            case HocClkVoltage_SOC:
                rc = rgltrOpenSession(&session, PcvPowerDomainId_Max77620_Sd0);
                ASSERT_RESULT_OK(rc, "rgltrOpenSession")
                rgltrGetVoltage(&session, &out);
                rgltrCloseSession(&session);
                break;
            case HocClkVoltage_EMCVDD2:
                rc = rgltrOpenSession(&session, PcvPowerDomainId_Max77620_Sd1);
                ASSERT_RESULT_OK(rc, "rgltrOpenSession")
                rgltrGetVoltage(&session, &out);
                rgltrCloseSession(&session);
                break;
            case HocClkVoltage_CPU:
                if (GetSocType() == HocClkSocType_Mariko) {
                    rc = rgltrOpenSession(&session, PcvPowerDomainId_Max77621_Cpu);
                } else {
                    rc = rgltrOpenSession(&session, PcvPowerDomainId_Max77812_Cpu);
                }
                ASSERT_RESULT_OK(rc, "rgltrOpenSession")
                rgltrGetVoltage(&session, &out);
                rgltrCloseSession(&session);
                break;
            case HocClkVoltage_GPU:
                if (GetSocType() == HocClkSocType_Mariko) {
                    rc = rgltrOpenSession(&session, PcvPowerDomainId_Max77621_Gpu);
                } else {
                    rc = rgltrOpenSession(&session, PcvPowerDomainId_Max77812_Gpu);
                }
                ASSERT_RESULT_OK(rc, "rgltrOpenSession")
                rgltrGetVoltage(&session, &out);
                rgltrCloseSession(&session);
                break;
            case HocClkVoltage_EMCVDDQ:
                if (GetSocType() == HocClkSocType_Mariko) {
                    rc = rgltrOpenSession(&session, PcvPowerDomainId_Max77812_Dram);
                    ASSERT_RESULT_OK(rc, "rgltrOpenSession")
                    rgltrGetVoltage(&session, &out);
                    rgltrCloseSession(&session);
                } else {
                    out = GetVoltage(HocClkVoltage_EMCVDD2);
                }
                break;
            case HocClkVoltage_Display:
                rc = rgltrOpenSession(&session, PcvPowerDomainId_Max77620_Ldo0);
                ASSERT_RESULT_OK(rc, "rgltrOpenSession")
                rgltrGetVoltage(&session, &out);
                rgltrCloseSession(&session);
                break;
            case HocClkVoltage_Battery:
                batteryInfoGetChargeInfo(&info);
                out = info.VoltageAvg;
                break;
            default:
                ASSERT_ENUM_VALID(HocClkVoltage, voltage);
        }

        return out > 0 ? out : 0;
    }

    Handle GetPcvHandle() {
        constexpr u64 PcvID = 0x10000000000001a;
        u64 processIDList[80]{};
        s32 processCount    = 0;
        Handle handle       = INVALID_HANDLE;

        DebugEventInfo debugEvent{};

        /* Get all running processes. */
        Result resultGetProcessList = svcGetProcessList(&processCount, processIDList, std::size(processIDList));
        if (R_FAILED(resultGetProcessList)) {
            return INVALID_HANDLE;
        }

        /* Try to find pcv. */
        for (int i = 0; i < processCount; ++i) {
            if (handle != INVALID_HANDLE) {
                svcCloseHandle(handle);
                handle = INVALID_HANDLE;
            }

            /* Try to debug process, if it fails, try next process. */
            Result resultSvcDebugProcess = svcDebugActiveProcess(&handle, processIDList[i]);
            if (R_FAILED(resultSvcDebugProcess)) {
                continue;
            }

            /* Try to get a debug event. */
            Result resultDebugEvent = svcGetDebugEvent(&debugEvent, handle);
            if (R_SUCCEEDED(resultDebugEvent)) {
                if (debugEvent.info.create_process.program_id == PcvID) {
                    return handle;
                }
            }
        }

        /* Failed to get handle. */
        return INVALID_HANDLE;
    }

    void CacheGpuVoltTable() {
        UnkRegulator reg = {
            .voltageMin  = 600000,
            .voltageStep = 12500,
            .voltageMax  = 1400000,
        };

        Handle handle = GetPcvHandle();
        if (handle == INVALID_HANDLE) {
            fileUtils::LogLine("[dvfs] Invalid handle!");
            return;
        }

        MemoryInfo memoryInfo = {};
        u64 address = 0;
        u32 pageInfo = 0;
        constexpr u32 PageSize = 0x1000;
        u8 buffer[PageSize];

        /* Loop until failure. */
        while (true) {
            /* Find pcv heap. */
            while (true) {
                Result resultProcessMemory = svcQueryDebugProcessMemory(&memoryInfo, &pageInfo, handle, address);
                address = memoryInfo.addr + memoryInfo.size;

                if (R_FAILED(resultProcessMemory) || !address) {
                    svcCloseHandle(handle);
                    fileUtils::LogLine("[dvfs] Failed to get process data. %u", R_DESCRIPTION(resultProcessMemory));
                    handle = INVALID_HANDLE;
                    return;
                }

                if (memoryInfo.size && (memoryInfo.perm & 3) == 3 && static_cast<char>(memoryInfo.type) == 0x4) {
                    /* Found valid memory. */
                    break;
                }
            }

            for (u64 base = 0; base < memoryInfo.size; base += PageSize) {
                u32 memorySize = std::min(memoryInfo.size, static_cast<u64>(PageSize));
                if (R_FAILED(svcReadDebugProcessMemory(buffer, handle, base + memoryInfo.addr, memorySize))) {
                    break;
                }

                u8 *resultFindReg = static_cast<u8 *>(memmem_impl(buffer, sizeof(buffer), &reg, sizeof(reg)));
                u32 index = resultFindReg - buffer;

                if (!resultFindReg) {
                    continue;
                }

                /* Assuming mariko. */
                const u32 vmax = 800;
                constexpr u32 GpuVoltageTableOffset = 312;
                if (!std::memcmp(&buffer[index + GpuVoltageTableOffset], &vmax, sizeof(vmax))) {
                    std::memcpy(voltData.voltTable, &buffer[index + GpuVoltageTableOffset], sizeof(voltData.voltTable));
                    voltData.voltTableAddress = base + memoryInfo.addr + GpuVoltageTableOffset + index;
                }

                constexpr u32 CpuVoltageTableOffset = 0xB8;
                std::memcpy(cpuVoltTable, &buffer[index + CpuVoltageTableOffset], sizeof(cpuVoltTable)); // TODO: verify the CPU table

                svcCloseHandle(handle);
                handle = INVALID_HANDLE;

                for(int i = 0; i < (int)std::size(cpuVoltTable); ++i) {
                    fileUtils::LogLine("[dvfs] cpu volt %d: %u mV", i, cpuVoltTable[i]);
                }

                for(int i = 0; i < (int)std::size(voltData.voltTable); ++i) {
                    fileUtils::LogLine("[dvfs] gpu volt %d: %u mV", i, voltData.voltTable[i]);
                }
                return;
            }
        }

        svcCloseHandle(handle);
        handle = INVALID_HANDLE;
        return;
    }

    void PcvHijackGpuVolts(u32 vmin) {
        u32 table[192];
        static_assert(sizeof(table) == sizeof(voltData.voltTable), "Invalid gpu voltage table size!");
        std::memcpy(table, voltData.voltTable, sizeof(voltData.voltTable));

        if (voltData.ramVmin == vmin) {
            return;
        }

        for (u32 i = 0; i < std::size(table); ++i) {
            if (table[i] && table[i] <= vmin) {
                table[i] = vmin;
            }
        }

        Handle handle = GetPcvHandle();
        if (handle == INVALID_HANDLE) {
            fileUtils::LogLine("Invalid handle!");
            return;
        }

        Result rc = svcWriteDebugProcessMemory(handle, table, voltData.voltTableAddress, sizeof(table));

        if (R_SUCCEEDED(rc)) {
            voltData.ramVmin = vmin;
        }

        svcCloseHandle(handle);
        fileUtils::LogLine("[dvfs] voltage set to %u mV", vmin);
    }

    u32 GetMinimumGpuVmin(u32 freqMhz, u32 bracket) {
        static const u32 ramTable[][22] = {
            { 2133, 2200, 2266, 2300, 2366, 2400, 2433, 2466, 2533, 2566, 2600, 2633, 2700, 2733, 2766, 2833, 2866, 2900, 2933, 3033, 3066, 3100, },
            { 2300, 2366, 2433, 2466, 2533, 2566, 2633, 2700, 2733, 2800, 2833, 2900, 2933, 2966, 3033, 3066, 3100, 3133, 3166, 3200, 3233, 3266, },
            { 2433, 2466, 2533, 2600, 2666, 2733, 2766, 2800, 2833, 2866, 2933, 2966, 3033, 3066, 3100, 3133, 3166, 3200, 3233, 3300, 3333, 3366, },
            { 2500, 2533, 2600, 2633, 2666, 2733, 2800, 2866, 2900, 2966, 3033, 3100, 3166, 3200, 3233, 3266, 3300, 3333, 3366, 3400, 3400, 3400, },
        };

        static const u32 gpuVoltArray[] = { 590, 600, 610, 620, 630, 640, 650, 660, 670, 680, 690, 700, 710, 720, 730, 740, 750, 760, 770, 780, 790, 800, };

        if (freqMhz <= 1600) return 0;
        if (bracket >= std::size(ramTable)) bracket = 0;

        u32 bracketStart = ramTable[bracket][0];
        
    
        u32 rampStartVolt = (bracket == 0) ? 535 : 525;
        u32 rampSpan = 590 - rampStartVolt; 


        if (freqMhz >= 1633 && freqMhz < bracketStart) {
            u32 raw = rampStartVolt + ((freqMhz - 1633) * rampSpan) / (bracketStart - 1633);
            u32 volt = ((raw + 2) / 5) * 5;
            if (volt < rampStartVolt) volt = rampStartVolt;
            if (volt > 590) volt = 590;
            return volt;
        }


        u32 baseVolt = gpuVoltArray[std::size(gpuVoltArray) - 1];
        for (u32 i = 0; i < std::size(gpuVoltArray); ++i) {
            if (freqMhz <= ramTable[bracket][i]) {
                baseVolt = gpuVoltArray[i];
                break;
            }
        }

        return baseVolt;
    }

}