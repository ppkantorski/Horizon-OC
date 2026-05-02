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

#include "integrations.hpp"
#include <sys/stat.h>
#include <SaltyNX.h>
#include "process_management.hpp"

namespace integrations {

    namespace {

        NxFpsSharedBlock* gNxFps = nullptr;
        SharedMemory gSharedMemory = {};
        bool gSharedMemoryUsed = false;
        Handle gRemoteSharedMemory = 1;
        u64 gPrevTid = 0;

        // ReverseNX-RT shared block layout (MAGIC "NXRT" = 0x5452584E).
        // Matches the Shared struct in ReverseNX-RT's overlay source exactly.
        struct ReverseNXRTBlock {
            uint32_t MAGIC;    // 0x5452584E
            bool isDocked;     // true = forced docked, false = forced handheld
            bool def;          // true = no override active (use hardware default)
            bool pluginActive;
            // remaining fields (res, wasDDRused) not needed
        } NX_PACKED;

        ReverseNXRTBlock* gRnxRT = nullptr;

        bool CheckSaltyNXPort() {
            Handle saltysd;

            for (int i = 0; i < 67; i++) {
                if (R_SUCCEEDED(svcConnectToNamedPort(&saltysd, "InjectServ"))) {
                    svcCloseHandle(saltysd);
                    break;
                }
                if (i == 66) return false;
                svcSleepThread(1'000'000);
            }

            for (int i = 0; i < 67; i++) {
                if (R_SUCCEEDED(svcConnectToNamedPort(&saltysd, "InjectServ"))) {
                    svcCloseHandle(saltysd);
                    return true;
                }
                svcSleepThread(1'000'000);
            }

            return false;
        }

        void SearchSharedMemoryBlock(uintptr_t base) {
            ptrdiff_t search_offset = 0;
            while (search_offset < 0x1000) {
                gNxFps = (NxFpsSharedBlock*)(base + search_offset);
                if (gNxFps->MAGIC == 0x465053)
                    return;
                search_offset += 4;
            }
            gNxFps = nullptr;
        }

        void SearchRnxRTBlock(uintptr_t base) {
            for (ptrdiff_t off = 0; off < 0x1000; off += 4) {
                auto* b = reinterpret_cast<ReverseNXRTBlock*>(base + off);
                if (b->MAGIC == 0x5452584E) {
                    gRnxRT = b;
                    return;
                }
            }
            gRnxRT = nullptr;
        }

        void LoadSharedMemory() {
            if (SaltySD_Connect())
                return;
            SaltySD_GetSharedMemoryHandle(&gRemoteSharedMemory);
            SaltySD_Term();
            shmemLoadRemote(&gSharedMemory, gRemoteSharedMemory, 0x1000, Perm_Rw);
            if (!shmemMap(&gSharedMemory))
                gSharedMemoryUsed = true;
        }

    }

    bool GetSysDockState() {
        struct stat st = {0};
        return stat("sdmc:/atmosphere/contents/42000000000000A0/flags/boot2.flag", &st) == 0;
    }

    bool GetSaltyNXState() {
        struct stat st = {0};
        return stat("sdmc:/atmosphere/contents/0000000000534C56/flags/boot2.flag", &st) == 0;
    }

    bool GetRETROSuperStatus() {
        struct stat st = {0};
        return stat("sdmc:/config/" CONFIG_DIR "/retro.flag", &st) == 0; // TODO: unhardcode this
    }

    void LoadSaltyNX() {
        if (!CheckSaltyNXPort())
            return;
        LoadSharedMemory();
    }

    u8 GetSaltyNXFPS() {
        if (!gSharedMemoryUsed)
            return 254;

        u64 tid = processManagement::GetCurrentApplicationId();
        if (tid == 0)
            return 254;

        if (gPrevTid != tid) {
            gNxFps = nullptr;
            gPrevTid = tid;
        }

        if (!gNxFps) {
            uintptr_t base = (uintptr_t)shmemGetAddr(&gSharedMemory);
            SearchSharedMemoryBlock(base);
        }

        return gNxFps ? gNxFps->FPS : 254;
    }

    u16 GetSaltyNXResolutionHeight() {
        if (!gSharedMemoryUsed)
            return 0;

        u64 tid = processManagement::GetCurrentApplicationId();
        if (tid == 0)
            return 0;

        if (gPrevTid != tid) {
            gNxFps = nullptr;
            gPrevTid = tid;
        }

        if (!gNxFps) {
            uintptr_t base = (uintptr_t)shmemGetAddr(&gSharedMemory);
            SearchSharedMemoryBlock(base);
        }

        if (gNxFps) {
            gNxFps->renderCalls[0].calls = 0xFFFF;
            svcSleepThread(10*1000);
            return gNxFps->renderCalls[0].height == 0 ? gNxFps->viewportCalls[0].height : gNxFps->renderCalls[0].height;
        }
        return 0;
    }

    u8 GetDisplaySync() {
        if (!gSharedMemoryUsed)
            return 0;

        u64 tid = processManagement::GetCurrentApplicationId();
        if (tid == 0)
            return 0;

        if (gPrevTid != tid) {
            gNxFps = nullptr;
            gRnxRT = nullptr;
            gPrevTid = tid;
        }

        uintptr_t base = (uintptr_t)shmemGetAddr(&gSharedMemory);

        // Check ReverseNX-RT block first (MAGIC "NXRT" = 0x5452584E).
        // isDocked tells us the forced mode; def=true means no override is active.
        if (!gRnxRT)
            SearchRnxRTBlock(base);
        if (gRnxRT && gRnxRT->pluginActive && !gRnxRT->def)
            return gRnxRT->isDocked ? 0x02 : 0x01;

        // Fall back to NxFps displaySync (bit 0 = handheld, bit 1 = docked).
        if (!gNxFps)
            SearchSharedMemoryBlock(base);
        return gNxFps ? gNxFps->displaySync.general : 0;
    }

}