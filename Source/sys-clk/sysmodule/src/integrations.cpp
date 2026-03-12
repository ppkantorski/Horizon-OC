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
    if (stat("sdmc:/atmosphere/contents/42000000000000A0", &st) == 0 && S_ISDIR(st.st_mode)) {
        return true;
    } else {
        return false;
    }
}

SaltyNXIntegration::SaltyNXIntegration() {
    if(!CheckPort()) return;
    LoadSharedMemory();
}


//Check if SaltyNX is working
bool SaltyNXIntegration::CheckPort () {
    Handle saltysd;
    for (int i = 0; i < 67; i++) {
        if (R_SUCCEEDED(svcConnectToNamedPort(&saltysd, "InjectServ"))) {
            svcCloseHandle(saltysd);
            break;
        }
        else {
            if (i == 66) return false;
            svcSleepThread(1'000'000);
        }
    }
    for (int i = 0; i < 67; i++) {
        if (R_SUCCEEDED(svcConnectToNamedPort(&saltysd, "InjectServ"))) {
            svcCloseHandle(saltysd);
            return true;
        }
        else svcSleepThread(1'000'000);
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
    while(search_offset < 0x1000) {
        NxFps = (NxFpsSharedBlock*)(base + search_offset);
        if (NxFps -> MAGIC == 0x465053) {
            return;
        }
        else search_offset += 4;
    }
    NxFps = 0;
    return;
}

u64 prevTid = 0;

u8 SaltyNXIntegration::GetFPS() {
    if(ProcessManagement::GetCurrentApplicationId() <= 0x010000000000FFFFULL) return 254; // only try to read fps for games, not system apps
    if(prevTid != ProcessManagement::GetCurrentApplicationId()) {
				uintptr_t base = (uintptr_t)shmemGetAddr(&_sharedmemory);
				searchSharedMemoryBlock(base);
                        prevTid = ProcessManagement::GetCurrentApplicationId();
    }
    if (NxFps) {
        return NxFps->FPS;
    } else {
        return 254;
    }
}