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
 

#include "integrations.h"
#include <sys/stat.h>
#include <SaltyNX.h>
#include "process_management.h"

SysDockIntegration::SysDockIntegration() {
}

bool SysDockIntegration::getCurrentSysDockState() {
    struct stat st = {0};
    return stat("sdmc:/atmosphere/contents/42000000000000A0/flags/boot2.flag", &st) == 0;
}

SaltyNXIntegration::SaltyNXIntegration() {
}

void SaltyNXIntegration::LoadSaltyNX() {
    if (!CheckPort())
        return;
    LoadSharedMemory();
}

bool SaltyNXIntegration::getCurrentSaltyNXState() {
    struct stat st = {0};
    return stat("sdmc:/atmosphere/contents/0000000000534C56/flags/boot2.flag", &st) == 0;
}
 
bool SaltyNXIntegration::CheckPort() {
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
 
void SaltyNXIntegration::LoadSharedMemory() {
    if (SaltySD_Connect())
        return;
    SaltySD_GetSharedMemoryHandle(&remoteSharedMemory);
    SaltySD_Term();
    shmemLoadRemote(&_sharedmemory, remoteSharedMemory, 0x1000, Perm_Rw);
    if (!shmemMap(&_sharedmemory))
        SharedMemoryUsed = true;
}
 
void SaltyNXIntegration::searchSharedMemoryBlock(uintptr_t base) {
    ptrdiff_t search_offset = 0;
    while (search_offset < 0x1000) {
        NxFps = (NxFpsSharedBlock*)(base + search_offset);
        if (NxFps->MAGIC == 0x465053)
            return;
        search_offset += 4;
    }
    NxFps = 0;
}
 
u64 prevTid = 0;
u8 SaltyNXIntegration::GetFPS() {
    if (!SharedMemoryUsed)
        return 254;
 
    u64 tid = ProcessManagement::GetCurrentApplicationId();
    if (tid == 0)
        return 254;
 
    if (prevTid != tid) {
        NxFps = 0;
        prevTid = tid;
    }
 
    if (!NxFps) {
        uintptr_t base = (uintptr_t)shmemGetAddr(&_sharedmemory);
        searchSharedMemoryBlock(base);
    }
 
    return NxFps ? NxFps->FPS : 254;
}