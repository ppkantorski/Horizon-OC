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
#include <registers.h>
#include <battery.h>
#include "display_refresh_rate.hpp"
#include <rgltr.h>
#include <notification.h>

#include "board.hpp"
#include "board_fuse.hpp"
#include "board_load.hpp"
#include "board_volt.hpp"
#include "board_misc.hpp"
#include "../soctherm.hpp"

namespace board {

    SysClkSocType gSocType;
    u8 gDramID;
    HorizonOCConsoleType gConsoleType = HorizonOCConsoleType_Iowa;
    FuseData fuseData;
    u8 speedoBracket;
    PwmChannelSession iCon;

    u32 fd = 0, fd2 = 0;

    void FetchHardwareInfos() {
        ReadFuses(fuseData);
        SetGpuBracket(fuseData.gpuSpeedo, speedoBracket);

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

        gConsoleType = static_cast<HorizonOCConsoleType>(sku);
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

        rc = apmExtInitialize();
        ASSERT_RESULT_OK(rc, "apmExtInitialize");

        rc = psmInitialize();
        ASSERT_RESULT_OK(rc, "psmInitialize");

        if(HOSSVC_HAS_TC) {
            rc = tcInitialize();
            ASSERT_RESULT_OK(rc, "tcInitialize");
        }

        rc = max17050Initialize();
        ASSERT_RESULT_OK(rc, "max17050Initialize");

        rc = tmp451Initialize();
        ASSERT_RESULT_OK(rc, "tmp451Initialize");

        Result nvCheck = 1;
        if (R_SUCCEEDED(nvInitialize())) {
            nvCheck = nvOpen(&fd, "/dev/nvhost-ctrl-gpu");
            Result nvCheck_sched = nvOpen(&fd2, "/dev/nvsched-ctrl");
            /* This can be improved. */
            NvSchedSucceed(nvCheck_sched);

            if (R_SUCCEEDED(nvCheck_sched)) {
                SchedSetFD2(fd2);
            }
        }

        rc = rgltrInitialize();
        ASSERT_RESULT_OK(rc, "rgltrInitialize");

        rc = pmdmntInitialize();
        ASSERT_RESULT_OK(rc, "pmdmntInitialize");

        StartLoad(nvCheck, fd);

        batteryInfoInitialize();
        FetchHardwareInfos();

        soctherm::Initialize();

        Result pwmCheck = 1;
        if (hosversionAtLeast(6,0,0) && R_SUCCEEDED(pwmInitialize())) {
            pwmCheck = pwmOpenSession2(&iCon, 0x3D000001);
        }

        StartMiscThread(pwmCheck, &iCon);

        u64 clkVirtAddr, dsiVirtAddr, outsize;

        rc = svcQueryMemoryMapping(&clkVirtAddr, &outsize, 0x60006000, 0x1000);
        ASSERT_RESULT_OK(rc, "svcQueryMemoryMapping (clk)");

        rc = svcQueryMemoryMapping(&dsiVirtAddr, &outsize, 0x54300000, 0x40000);
        ASSERT_RESULT_OK(rc, "svcQueryMemoryMapping (dsi)");

        display::DisplayRefreshConfig cfg = {.clkVirtAddr = clkVirtAddr, .dsiVirtAddr = dsiVirtAddr};
        display::Initialize(&cfg);

        CacheDfllData();
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

        pwmChannelSessionClose(&iCon);
        pwmExit();
        rgltrExit();
        batteryInfoExit();
        pmdmntExit();
        nvExit();

        display::Shutdown();
    }

    SysClkSocType GetSocType() {
        return gSocType;
    }

    HorizonOCConsoleType GetConsoleType() {
        return gConsoleType;
    }

    u8 GetDramID() {
        return gDramID;
    }

    bool IsDram8GB() {
        SecmonArgs args = {};
        args.X[0] = 0xF0000002;
        args.X[1] = MC_REGISTER_BASE + MC_EMEM_CFG_0;
        svcCallSecureMonitor(&args);

        if (args.X[1] == (MC_REGISTER_BASE + MC_EMEM_CFG_0)) { // if param 1 is identical read failed
            notification::writeNotification("Horizon OC\nSecmon read failed!\n This may be a hardware issue!");
            return false;
        }

        return args.X[1] == 0x00002000 ? true : false;
    }

    /* TODO: Put this into a different file. */
    void SetDisplayRefreshDockedState(bool docked) {
        if (GetConsoleType() != HorizonOCConsoleType_Hoag) {
            display::SetDockedState(docked);
        }
    }

    FuseData *GetFuseData() {
        return &fuseData;
    }

    u8 GetGpuSpeedoBracket() {
        return speedoBracket;
    }

    bool IsUsingRetroSuperDisplay() {
        return false; /* stub for now. */
    }

}
