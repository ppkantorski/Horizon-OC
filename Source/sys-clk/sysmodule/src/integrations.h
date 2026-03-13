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
 

#pragma once
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <nxExt.h>
#include <sysclk.h>
#include <switch.h>
#include "errors.h"
#include "file_utils.h"

#include "clock_manager.h"

class SysDockIntegration {
public:
    SysDockIntegration();

    bool getCurrentSysDockState();
};

class SaltyNXIntegration {
public:
    struct resolutionCalls {
        uint16_t width;
        uint16_t height;
        uint16_t calls;
    };

    struct NxFpsSharedBlock {
        uint32_t MAGIC;
        uint8_t FPS;
        float FPSavg;
        bool pluginActive;
        uint8_t FPSlocked;
        uint8_t FPSmode;
        uint8_t ZeroSync;
        uint8_t patchApplied;
        uint8_t API;
        uint32_t FPSticks[10];
        uint8_t Buffers;
        uint8_t SetBuffers;
        uint8_t ActiveBuffers;
        uint8_t SetActiveBuffers;
        union {
           struct {
                bool handheld: 1;
                bool docked: 1;
                unsigned int reserved: 6;
            } NX_PACKED ds;
            uint8_t general;
        } displaySync;
        resolutionCalls renderCalls[8];
        resolutionCalls viewportCalls[8];
        bool forceOriginalRefreshRate;
        bool dontForce60InDocked;
        bool forceSuspend;
        uint8_t currentRefreshRate;
        float readSpeedPerSecond;
        uint8_t FPSlockedDocked;
        uint64_t frameNumber;
    } NX_PACKED;

    NxFpsSharedBlock* NxFps = 0;
    SharedMemory _sharedmemory = {};
    bool SharedMemoryUsed = false;
    Handle remoteSharedMemory = 1;
    SaltyNXIntegration();
    void LoadSaltyNX();
    bool getCurrentSaltyNXState();

    bool CheckPort();
    void LoadSharedMemory();
    void searchSharedMemoryBlock(uintptr_t base);
    u8 GetFPS();
};