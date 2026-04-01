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
#include <sysclk.h>
#include <nxExt.h>
#include "display_refresh_rate.hpp"
#include "board.hpp"
#include "board_name.hpp"
#include "../errors.hpp"

namespace board {

    PcvModule GetPcvModule(SysClkModule sysclkModule) {
        switch (sysclkModule) {
            case SysClkModule_CPU:
                return PcvModule_CpuBus;
            case SysClkModule_GPU:
                return PcvModule_GPU;
            case SysClkModule_MEM:
                return PcvModule_EMC;
            default:
                ASSERT_ENUM_VALID(SysClkModule, sysclkModule);
        }

        return static_cast<PcvModule>(0);
    }

    PcvModuleId GetPcvModuleId(SysClkModule sysclkModule) {
        PcvModuleId pcvModuleId;
        Result rc = pcvGetModuleId(&pcvModuleId, GetPcvModule(sysclkModule));
        ASSERT_RESULT_OK(rc, "pcvGetModuleId");

        return pcvModuleId;
    }

    void ClkrstSetHz(ClkrstSession &session, u32 hz) {
        ASSERT_RESULT_OK(clkrstSetClockRate(&session, hz), "clkrstSetClockRate");
    }

    void PcvSetHz(PcvModule moduleID, u32 hz) {
        ASSERT_RESULT_OK(pcvSetClockRate(moduleID, hz), "pcvSetClockRate");
    }

    void SetHz(SysClkModule module, u32 hz) {
        Result rc = 0;
        bool usesGovenor = module > SysClkModule_MEM;


        if (module == HorizonOCModule_Display) {
            display::SetRate(hz);
            return;
        }

        if (usesGovenor) {
            return;
        }

        if (HOSSVC_HAS_CLKRST) {
            ClkrstSession session = {};
            rc = clkrstOpenSession(&session, GetPcvModuleId(module), 3);
            ASSERT_RESULT_OK(rc, "clkrstOpenSession");
            ClkrstSetHz(session, hz);

            /* Voltage bug workaround. */
            if (module == SysClkModule_CPU) {
                svcSleepThread(250'000);
                ClkrstSetHz(session, hz);
            }

            clkrstCloseSession(&session);
        } else {
            PcvSetHz(GetPcvModule(module), hz);

            if (module == SysClkModule_CPU) {
                svcSleepThread(250'000);
                PcvSetHz(GetPcvModule(module), hz);
            }
        }
    }

    u32 GetDisplayRate(u32 hz) {
        display::GetRate(&hz, false);
        return hz;
    }

    u32 GetHz(SysClkModule module) {
        Result rc = 0;
        u32 hz = 0;

        if (module == HorizonOCModule_Display) {
            return GetDisplayRate(hz);
        }

        if (HOSSVC_HAS_CLKRST) {
            ClkrstSession session = {};

            rc = clkrstOpenSession(&session, GetPcvModuleId(module), 3);
            ASSERT_RESULT_OK(rc, "clkrstOpenSession");

            rc = clkrstGetClockRate(&session, &hz);
            ASSERT_RESULT_OK(rc, "clkrstGetClockRate");

            clkrstCloseSession(&session);
        } else {
            rc = pcvGetClockRate(GetPcvModule(module), &hz);
            ASSERT_RESULT_OK(rc, "pcvGetClockRate");
        }

        return hz;
    }

    u32 GetRealHz(SysClkModule module) {
        u32 hz = 0;
        switch (module) {
            case SysClkModule_CPU:
                return t210ClkCpuFreq();
            case SysClkModule_GPU:
                return t210ClkGpuFreq();
            case SysClkModule_MEM:
                return t210ClkMemFreq();
            case HorizonOCModule_Display:
                return GetDisplayRate(hz);
            return hz;
            default:
                ASSERT_ENUM_VALID(SysClkModule, module);
        }

        return 0;
    }

    void GetFreqList(SysClkModule module, u32 *outList, u32 maxCount, u32 *outCount) {
        Result rc = 0;
        PcvClockRatesListType type;
        s32 tmpInMaxCount = maxCount;
        s32 tmpOutCount = 0;


        if (HOSSVC_HAS_CLKRST) {
            ClkrstSession session = {};

            rc = clkrstOpenSession(&session, GetPcvModuleId(module), 3);
            ASSERT_RESULT_OK(rc, "clkrstOpenSession");

            rc = clkrstGetPossibleClockRates(&session, outList, tmpInMaxCount, &type, &tmpOutCount);
            ASSERT_RESULT_OK(rc, "clkrstGetPossibleClockRates");

            clkrstCloseSession(&session);
        } else {
            rc = pcvGetPossibleClockRates(GetPcvModule(module), outList, tmpInMaxCount, &type, &tmpOutCount);
            ASSERT_RESULT_OK(rc, "pcvGetPossibleClockRates");
        }

        if (type != PcvClockRatesListType_Discrete) {
            ERROR_THROW("Unexpected PcvClockRatesListType: %u (module = %s)", type, GetModuleName(module, false));
        }

        *outCount = tmpOutCount;
    }

    u32 GetHighestDockedDisplayRate() {
        if (GetConsoleType() != HorizonOCConsoleType_Hoag) {
            return display::GetDockedHighestAllowed();
        }

        return 60;
    }

    void ResetToStock() {
        Result rc;
        if (hosversionAtLeast(9,0,0)) {
            std::uint32_t confId = 0;
            rc = apmExtGetCurrentPerformanceConfiguration(&confId);
            ASSERT_RESULT_OK(rc, "apmExtGetCurrentPerformanceConfiguration");

            SysClkApmConfiguration* apmConfiguration = nullptr;
            for (size_t i = 0; sysclk_g_apm_configurations[i].id; ++i) {
                if(sysclk_g_apm_configurations[i].id == confId) {
                    apmConfiguration = &sysclk_g_apm_configurations[i];
                    break;
                }
            }

            if(!apmConfiguration) {
                ERROR_THROW("Unknown apm configuration: %x", confId);
            }

            SetHz(SysClkModule_CPU, apmConfiguration->cpu_hz);
            SetHz(SysClkModule_GPU, apmConfiguration->gpu_hz);
            SetHz(SysClkModule_MEM, apmConfiguration->mem_hz);
        } else {
            u32 mode = 0;
            rc = apmExtGetPerformanceMode(&mode);
            ASSERT_RESULT_OK(rc, "apmExtGetPerformanceMode");

            rc = apmExtSysRequestPerformanceMode(mode);
            ASSERT_RESULT_OK(rc, "apmExtSysRequestPerformanceMode");
        }
    }

    void ResetToStockDisplay() {
        if (GetConsoleType() != HorizonOCConsoleType_Hoag) {
            display::SetRate(60);
        }
    }

}
