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
#pragma once
#include <atomic>
#include <sysclk.h>
#include <switch.h>
#include "config.h"
#include "board.h"
#include <nxExt/cpp/lockable_mutex.h>
#include "integrations.h"

class SysDockIntegration;
class SaltyNXIntegration;
class ClockManager
{
  public:
     /**
     * Get instance
     * @return Pointer to a ClockManager instance
     */
    static ClockManager* GetInstance();
    static void Initialize();
    static void Exit();
    ClockManager();
    virtual ~ClockManager();

    /**
     * Get context object
     * @return Context instance
     */
    SysClkContext GetCurrentContext();

    /**
     * Get config object
     * @return Pointer to a config instance
     */
    Config* GetConfig();

    /**
     * Set clock manager running
     * @param running Is running or not?
     */
    void SetRunning(bool running);

    /**
     * Is clock manager running
     * @return running or not?
     */
    bool Running();

    /**
     * Get frequency list from clkrst
     *
     * @param module Module to get frequency list for
     * @param list List of frequencies
     * @param maxCount How many entries to expect in list. Usually 32
     * @param outCount How many entries were retrived
     */
    void GetFreqList(SysClkModule module, std::uint32_t* list, std::uint32_t maxCount, std::uint32_t* outCount);

    /**
     * Handles safety features
     *
     */
    void HandleSafetyFeatures();

    /**
     * Handles misc features (currently only battery charge current).
     *
     */
    void HandleMiscFeatures();

    /**
     * Handles governor state resolution and applies CPU/GPU governor transitions.
     *
     * @param targetHz   Governor override value for the current profile.
     */
    void HandleGovernor(uint32_t targetHz);

    /**
     * Handles DVFS logic before the frequency set
     *
     * @param targetHz   Governor override value for the current profile.
     */
    void DVFSBeforeSet(u32 targetHz);

    /**
     * Handles DVFS logic after the frequency set
     *
     * @param targetHz   Governor override value for the current profile.
     */
    void DVFSAfterSet(u32 targetHz);

    /**
     * Reset the GPU vMin
     *
     */
    void DVFSReset();

    /**
     * Handles the Live CPU UV Feature
     *
     */
    void HandleCpuUv();

    /**
     * Handles frequency resets
     *
     * @param module The module to reset frequency for
     * @param isBoost Is in boost mode
     */
    void HandleFreqReset(SysClkModule module, bool isBoost);

    /**
     * Sets clocks
     *
     * @param isBoost Is in boost mode
     */
    void SetClocks(bool isBoost);
    
    /**
     * Main function, runs every 5s in sleep mode, and a user specified amount when awake
     *
     */
    void Tick();

    /**
     * Reset CPU/GPU to stock values
     *
     */
    void ResetToStockClocks();

    /**
     * Wait for the next tick event
     *
     */
    void WaitForNextTick();

    /**
     * Set the data in the KIP
     *
     */
    void SetKipData();

    /**
     * Get the data from the KIP
     *
     */
    void GetKipData();

    /**
     * Runs the CPU Governor
     *
     * @param arg Cast to ClockManager* for context
     */
    static void CpuGovernorThread(void* arg);

    /**
     * Runs the GPU Governor
     *
     * @param arg Cast to ClockManager* for context
     */
    static void GovernorThread(void* arg);

    /**
     * Runs the VRR Algorithm
     *
     * @param arg Cast to ClockManager* for context
     */
    static void VRRThread(void* arg);

    /**
     * Gets the effective governor state from application/temporary override
     *
     * @param appState Governor state from app
     * @param tempState Governor state from temporary override
     */
    GovernorState GetEffectiveGovernorState(GovernorState appState, GovernorState tempState);

    /**
     * Frequency table
     *
     */
    struct FreqTable {
      std::uint32_t count;
      std::uint32_t list[SYSCLK_FREQ_LIST_MAX];
    } freqTable[SysClkModule_EnumMax];

    /**
     * Gets the current GPU speedo bracket
     *
     * @param speedo GPU Speedo
     */
    int GetSpeedoBracket (int speedo);

    /**
     * Gets the required vMin for a ram frequency for a speedo
     *
     * @param freq RAM Freq in MHz
     * @param speedo GPU Speedo
     */
    unsigned int GetGpuVoltage (unsigned int freq, int speedo);

    /**
     * Gets the required vMin for a ram frequency for a speedo
     *
     * @param util Utilization in percentile
     * @param tableMaxHz Table Max Hz
     */
    static u32 SchedutilTargetHz(u32 util, u32 tableMaxHz);

    /**
     * Gets the required vMin for a ram frequency for a speedo
     *
     * @param table FreqTable for module
     * @param targetHz Hz to search for
     */
    static u32 TableIndexForHz(const FreqTable& table, u32 targetHz);

    /**
     * Gets the required vMin for a ram frequency for a speedo
     *
     * @param mgr ClockManager instance (runs in a thread so must be passed)
     * @param module Module for which to resolve target Hz
     */
    static u32 ResolveTargetHz(ClockManager* mgr, SysClkModule module);

  protected:
    bool IsAssignableHz(SysClkModule module, std::uint32_t hz);
    inline std::uint32_t GetMaxAllowedHz(SysClkModule module, SysClkProfile profile);
    std::uint32_t GetNearestHz(SysClkModule module, std::uint32_t inHz, std::uint32_t maxHz);
    bool ConfigIntervalTimeout(SysClkConfigValue intervalMsConfigValue, std::uint64_t ns, std::uint64_t* lastLogNs);
    void RefreshFreqTableRow(SysClkModule module);
    bool RefreshContext();
    static ClockManager *instance;
    std::atomic_bool running;
    LockableMutex contextMutex;
    Config* config;
    SysClkContext* context;
    std::uint64_t lastTempLogNs;
    std::uint64_t lastFreqLogNs;
    std::uint64_t lastPowerLogNs;
    std::uint64_t lastCsvWriteNs;
    SysDockIntegration *sysDockIntegration;
    SaltyNXIntegration *saltyNXIntegration;
};