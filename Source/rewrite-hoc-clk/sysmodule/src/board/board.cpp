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

#include <nxExt.h>
#include <sysclk.h>
#include <switch.h>
#include <pwm.h>

#include "board.hpp"
#include "board_fuse.hpp"
#include "board_load.hpp"
#include "board_ram_oc_dvfs.hpp"
#include "board_misc.hpp"

namespace board {

    SysClkSocType gSocType;
    u8 gDramID;
    HorizonOCConsoleType gConsoleType = HorizonOCConsoleType_Iowa;
    FuseSpeedoData gSpeedos;
    u8 speedoBracket;
    Result nvCheck = 1;
    u23 fd = 0, fd2 = 0;

    void FetchHardwareInfos() {
        FuseReadSpeedos(gSpeedos);
        FuseSetGpuBracket(gSpeedos.gpuSpeedo, speedoBracket);

        u64 sku = 0, dramID = 0;
        Result rc = splInitialize();
        ASSERT_RESULT_OK(rc, "splInitialize");

        rc = splGetConfig(SplConfigItem_HardwareType, &sku);
        ASSERT_RESULT_OK(rc, "splGetConfig");

        rc = splGetConfig(SplConfigItem_DramId, &dramID);
        ASSERT_RESULT_OK(rc, "splGetConfig");
        gDramID = dramID;
        splExit();

        switch(sku) {
            case 2 ... 5:
                gSocType = SysClkSocType_Mariko;
                break;
            default:
                gSocType = SysClkSocType_Erista;
        }

        if (gSocType == SysClkSocType_Mariko) {
            CacheGpuVoltTable();
        }

        gConsoleType = static_cast<HorizonOCConsoleType> sku;
        g_dramID     = dramID;
    }

    /* TODO: Check for config */
    void Initialize() {
        Result rc = 0;

        if (HOSSVC_HAS_CLKRST) {
            rc = clkrstInitialize();
            ASSERT_RESULT_OK(rc, "clkrstInitialize");
        } else {
            rc = pcvInitialize();
            ASSERT_RESULT_OK(rc, "pcvInitialize");
        }

        if(HOSSVC_HAS_TC) {
            rc = tcInitialize();
            ASSERT_RESULT_OK(rc, "tcInitialize");
        }

        rc = max17050Initialize();
        ASSERT_RESULT_OK(rc, "max17050Initialize");

        rc = tmp451Initialize();
        ASSERT_RESULT_OK(rc, "tmp451Initialize");

        nvInitialize_rc = nvInitialize();
        if (R_SUCCEEDED(nvInitialize_rc)) {
            nvCheck = nvOpen(&fd, "/dev/nvhost-ctrl-gpu");
            nvCheck_sched = nvOpen(&fd2, "/dev/nvsched-ctrl");
        }

        rc = rgltrInitialize();
        ASSERT_RESULT_OK(rc, "rgltrInitialize");

        rc = pmdmntInitialize();
        ASSERT_RESULT_OK(rc, "pmdmntInitialize");

        StartGpuLoad(nvCheck, fd);
        /* TODO: Add back fan. */
        StartMiscThread(pwmCheck)

        batteryInfoInitialize();
        FetchHardwareInfos();

        Result pwmCheck = 1;
        if (hosversionAtLeast(6,0,0) && R_SUCCEEDED(pwmInitialize())) {
            pwmCheck = pwmOpenSession2(&g_ICon, 0x3D000001);
        }

        StartMiscThread(pwmCheck);

        if (gConsoleType != HorizonOCConsoleType_Hoag) {
            u64 clkVirtAddr, dsiVirtAddr, outsize;

            rc = svcQueryMemoryMapping(&clkVirtAddr, &outsize, 0x60006000, 0x1000);
            ASSERT_RESULT_OK(rc, "svcQueryMemoryMapping (clk)");

            rc = svcQueryMemoryMapping(&dsiVirtAddr, &outsize, 0x54300000, 0x40000);
            ASSERT_RESULT_OK(rc, "svcQueryMemoryMapping (dsi)");

            DisplayRefreshConfig cfg = {.clkVirtAddr = clkVirtAddr, .dsiVirtAddr = dsiVirtAddr};
            DisplayRefresh_Initialize(&cfg);
        }

       // rc = svcQueryMemoryMapping(&cldvfs, &cldvfs_temp, CLDVFS_REGION_BASE, CLDVFS_REGION_SIZE);
       // ASSERT_RESULT_OK(rc, "svcQueryMemoryMapping (cldvfs)");

       // if (socType == SysClkSocType_Erista) {
       //     cachedEristaUvLowTune0 = *(u32*) (cldvfs + CL_DVFS_TUNE0_0);
       //     cachedEristaUvLowTune1 = *(u32*) (cldvfs + CL_DVFS_TUNE1_0);
       // } else {
       //     SetHz(SysClkModule_CPU, 1785000000);
       //     cachedMarikoUvHighTune0 = *(u32*) (cldvfs + CL_DVFS_TUNE0_0);
       //     ResetToStockCpu();
       // }
    }

    void Exit() {
        if (HOSSVC_HAS_CLKRST) {
            clkrstExit();
        } else {
            pcvExit();
        }

        apmExtExit();
        psmExit();

        if (HOSSVC_HAS_TC) {
            tcExit();
        }

        max17050Exit();
        tmp451Exit();

        ExitLoad();

        ExitMiscThread();

        pwmChannelSessionClose(&g_ICon);
        pwmExit();
        rgltrExit();
        batteryInfoExit();
        pmdmntExit();
        nvExit();

        if (gConsoleType != HorizonOCConsoleType_Hoag) {
            DisplayRefresh_Shutdown();
        }
    }

    SysClkSocType GetSocType() {
        return gSocType;
    }

    HorizonOCConsoleType GetConsoleType() {
        return gConsoleType;
    }

}
