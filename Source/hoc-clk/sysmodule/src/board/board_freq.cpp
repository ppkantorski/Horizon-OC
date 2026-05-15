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
#include <hocclk.h>
#include "../hos/apm_ext.h"
#include <i2c.h>
#include "../i2c/i2cDrv.h"
#include <t210.h>
#include <max17050.h>
#include <tmp451.h>
#include <ipc_server.h>
#include <lockable_mutex.h>
#include "../display/display_refresh_rate.hpp"
#include "board.hpp"
#include "board_name.hpp"
#include "../file/errors.hpp"
#include "../soc/pllmb.hpp"
#include "../file/config.hpp"
#include "../soc/gm20b.hpp"
namespace board {
    #define MIDDLE_FREQ_TABLE_START_POINT 1228800000
    static u32 currentInjectedHz = 0;
    PcvModule GetPcvModule(HocClkModule hocclkModule) {
        switch (hocclkModule) {
            case HocClkModule_CPU:
                return PcvModule_CpuBus;
            case HocClkModule_GPU:
                return PcvModule_GPU;
            case HocClkModule_MEM:
                return PcvModule_EMC;
            default:
                ASSERT_ENUM_VALID(HocClkModule, hocclkModule);
        }

        return static_cast<PcvModule>(0);
    }

    PcvModuleId GetPcvModuleId(HocClkModule hocclkModule) {
        PcvModuleId pcvModuleId;
        Result rc = pcvGetModuleId(&pcvModuleId, GetPcvModule(hocclkModule));
        ASSERT_RESULT_OK(rc, "pcvGetModuleId");

        return pcvModuleId;
    }

    void ClkrstSetHz(ClkrstSession &session, u32 hz) {
        ASSERT_RESULT_OK(clkrstSetClockRate(&session, hz), "clkrstSetClockRate");
    }

    void PcvSetHz(PcvModule moduleID, u32 hz) {
        ASSERT_RESULT_OK(pcvSetClockRate(moduleID, hz), "pcvSetClockRate");
    }

    void SetHz(HocClkModule module, u32 hz) {
        Result rc = 0;
        bool usesGovenor = module > HocClkModule_MEM;

        if (module == HocClkModule_Display) {
            display::SetRate(hz);
            return;
        }

        if (usesGovenor) {
            return;
        }

        bool useGm20b = (module == HocClkModule_GPU) && (GetSocType() == HocClkSocType_Mariko) && (hz % 38400000 == 0) && (hz % 76800000 != 0) && hz < MIDDLE_FREQ_TABLE_START_POINT;

        u32 pcvHz = useGm20b ? ((hz + 76800000 - 1) / 76800000) * 76800000 : hz;

        if (module == HocClkModule_GPU)
            currentInjectedHz = 0;

        if (HOSSVC_HAS_CLKRST) {
            ClkrstSession session = {};
            rc = clkrstOpenSession(&session, GetPcvModuleId(module), 3);
            ASSERT_RESULT_OK(rc, "clkrstOpenSession");
            ClkrstSetHz(session, pcvHz);

            /* Voltage bug workaround. */
            if (module == HocClkModule_CPU) {
                svcSleepThread(300'000);
                ClkrstSetHz(session, pcvHz);
            }

            clkrstCloseSession(&session);
        } else {
            PcvSetHz(GetPcvModule(module), pcvHz);

            if (module == HocClkModule_CPU) {
                svcSleepThread(300'000);
                PcvSetHz(GetPcvModule(module), pcvHz);
            }
        }

        if (useGm20b) {
            gm20b::setClock(hz / 1000);
            currentInjectedHz = hz;
        }
    }

    u32 GetDisplayRate(u32 hz) {
        display::GetRate(&hz, false);
        return hz;
    }

    u32 GetHz(HocClkModule module) {
        Result rc = 0;
        u32 hz = 0;

        if (module == HocClkModule_Display) {
            return GetDisplayRate(hz);
        }

        if (module == HocClkModule_GPU && currentInjectedHz != 0) {
            return currentInjectedHz;
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

    u32 GetRealHz(HocClkModule module) {
        u32 hz = 0;
        switch (module) {
            case HocClkModule_CPU:
                return t210ClkCpuFreq();
            case HocClkModule_GPU:
                return t210ClkGpuFreq();
            case HocClkModule_MEM:
                return config::GetConfigValue(HocClkConfigValue_MemoryFrequencyMeasurementMode) == MemoryFrequencyMeasurementMode_PLL ? pllmb::getRamClockRatePLLMB() : t210ClkMemFreq();
            case HocClkModule_Display:
                return GetDisplayRate(hz);
            default:
                ASSERT_ENUM_VALID(HocClkModule, module);
        }

        return 0;
    }

    void GetFreqList(HocClkModule module, u32 *outList, u32 maxCount, u32 *outCount) {
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
        if (GetConsoleType() != HocClkConsoleType_Hoag) {
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

            HocClkApmConfiguration* apmConfiguration = nullptr;
            for (size_t i = 0; hocclk_g_apm_configurations[i].id; ++i) {
                if(hocclk_g_apm_configurations[i].id == confId) {
                    apmConfiguration = &hocclk_g_apm_configurations[i];
                    break;
                }
            }

            if(!apmConfiguration) {
                ERROR_THROW("Unknown apm configuration: %x", confId);
            }

            SetHz(HocClkModule_CPU, apmConfiguration->cpu_hz);
            SetHz(HocClkModule_GPU, apmConfiguration->gpu_hz);
            SetHz(HocClkModule_MEM, apmConfiguration->mem_hz);
        } else {
            u32 mode = 0;
            rc = apmExtGetPerformanceMode(&mode);
            ASSERT_RESULT_OK(rc, "apmExtGetPerformanceMode");

            rc = apmExtSysRequestPerformanceMode(mode);
            ASSERT_RESULT_OK(rc, "apmExtSysRequestPerformanceMode");
        }
    }

    void ResetToStockDisplay() {
        display::SetRate(60);
    }
}
