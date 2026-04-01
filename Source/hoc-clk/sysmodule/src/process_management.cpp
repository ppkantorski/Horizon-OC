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


#include "process_management.hpp"
#include "file_utils.hpp"
#include "errors.hpp"

namespace processManagement {

    namespace {
        constexpr u64 Qlaunch   = 0x0100000000001000ULL;
        constexpr u32 IsQlaunch = 0x20f;
    }

    void Initialize() {
        Result rc = 0;

        rc = pmdmntInitialize();
        ASSERT_RESULT_OK(rc, "pmdmntInitialize");

        rc = pminfoInitialize();
        ASSERT_RESULT_OK(rc, "pminfoInitialize");
    }

    void WaitForQLaunch() {

        Result rc = 0;
        u64 pid = 0;
        do {
            rc = pmdmntGetProcessId(&pid, Qlaunch);
            svcSleepThread(50 * 1000000ULL); // 50ms
        } while (R_FAILED(rc));
    }

    u64 GetCurrentApplicationId() {
        Result rc = 0;
        u64 pid = 0;
        u64 tid = 0;
        rc = pmdmntGetApplicationProcessId(&pid);

        if (rc == IsQlaunch) {
            return Qlaunch;
        }

        ASSERT_RESULT_OK(rc, "pmdmntGetApplicationProcessId");

        rc = pminfoGetProgramId(&tid, pid);

        if (rc == IsQlaunch) {
            return Qlaunch;
        }

        ASSERT_RESULT_OK(rc, "pminfoGetProgramId");

        return tid;
    }

    void Exit() {
        pmdmntExit();
        pminfoExit();
    }

}