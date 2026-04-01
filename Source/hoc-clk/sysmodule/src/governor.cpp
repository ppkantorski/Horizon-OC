#include "governor.hpp"

namespace governor {

    #define POLL_NS 5'000'000  // 5 ms  – governor poll rate
    #define DOWN_HOLD_TICKS 10         // 50 ms – how long to in POLL_NS to hold while ramping down
    #define STEP_UTIL 900        // multiplier for step calculations

    bool isGpuGovernorEnabled = false;
    bool isCpuGovernorEnabled = false;
    bool lastGpuGovernorState = false;
    bool lastCpuGovernorState = false;
    bool lastVrrGovernorState = false;
    bool hasChanged = true;
    bool isCpuGovernorInBoostMode = false;
    bool isVRREnabled = false;

    // thread handles
    Thread cpuGovernorTHREAD;
    Thread gpuGovernorTHREAD;
    Thread vrrTHREAD;

    void HandleGovernor(uint32_t targetHz)
    {
        u32 tempTargetHz = clockManager::gContext.overrideFreqs[HorizonOCModule_Governor];
        if (!tempTargetHz) {
            tempTargetHz = config::GetAutoClockHz(clockManager::gContext.applicationId, HorizonOCModule_Governor, clockManager::gContext.profile, true);
            if (!tempTargetHz)
                tempTargetHz = config::GetAutoClockHz(GLOBAL_PROFILE_ID, HorizonOCModule_Governor, clockManager::gContext.profile, true);
        }

        auto resolve = [](u8 app, u8 temp) -> u8 {
            if (temp == ComponentGovernor_Disabled) return ComponentGovernor_Disabled;
            if (temp != ComponentGovernor_DoNotOverride) return temp;
            return app;
        };

        u8 effectiveCpu = resolve(GovernorStateCpu(targetHz), GovernorStateCpu(tempTargetHz));
        u8 effectiveGpu = resolve(GovernorStateGpu(targetHz), GovernorStateGpu(tempTargetHz));
        u8 effectiveVrr = resolve(GovernorStateVrr(targetHz), GovernorStateVrr(tempTargetHz));

        bool newCpuGovernorState = (effectiveCpu == ComponentGovernor_Enabled);
        bool newGpuGovernorState = (effectiveGpu == ComponentGovernor_Enabled);
        bool newVrrGovernorState = (effectiveVrr == ComponentGovernor_Enabled);

        isCpuGovernorEnabled = newCpuGovernorState;
        isGpuGovernorEnabled = newGpuGovernorState;
        isVRREnabled = newVrrGovernorState;

        if (newCpuGovernorState == false && lastCpuGovernorState == true) {
            svcSleepThread(100'000'000); // thread syncing. probably a cleaner way to do this but hey, it works!
            board::ResetToStockCpu();
        }
        if (newGpuGovernorState == false && lastGpuGovernorState == true) {
            svcSleepThread(100'000'000);
            board::ResetToStockGpu();
        }
        if (newVrrGovernorState == false && lastVrrGovernorState == true) {
            svcSleepThread(100'000'000);
            board::ResetToStockDisplay();
        }
        if (newCpuGovernorState != lastCpuGovernorState || newGpuGovernorState != lastGpuGovernorState || newVrrGovernorState != lastVrrGovernorState) {
            fileUtils::LogLine("[mgr] Governor state changed: CPU %s, GPU %s, VRR %s", newCpuGovernorState ? "enabled" : "disabled", newGpuGovernorState ? "enabled" : "disabled", newVrrGovernorState ? "enabled" : "disabled");
            lastCpuGovernorState = newCpuGovernorState;
            lastGpuGovernorState = newGpuGovernorState;
            lastVrrGovernorState = newVrrGovernorState;
        }
    }

    u32 SchedutilTargetHz(u32 util, u32 tableMaxHz)
    {
        u64 hz = (u64)tableMaxHz * util / STEP_UTIL;
        return (u32)(std::min(hz, static_cast<u64>(tableMaxHz)));
    }

    u32 TableIndexForHz(const clockManager::FreqTable& table, u32 targetHz)
    {
        for (u32 i = 0; i < table.count; i++)
            if (table.list[i] >= targetHz)
                return i;
        return table.count - 1;
    }

    u32 ResolveTargetHz(SysClkModule module)
    {
        u32 hz = clockManager::gContext.overrideFreqs[module];
        if (!hz)
            hz = config::GetAutoClockHz(
                    clockManager::gContext.applicationId, module,
                    clockManager::gContext.profile, false);
        if (!hz)
            hz = config::GetAutoClockHz(
                    GLOBAL_PROFILE_ID, module,
                    clockManager::gContext.profile, false);
        return hz;
    }

    void CpuGovernorThread(void* arg)
    {
        (void)arg;

        u32 downHoldRemaining = 0;
        u32 lastHz            = 0;
        u32 minHz = 612;
        u32 tick = 0;
        for (;;) {
            if (!clockManager::gRunning || !isCpuGovernorEnabled) {
                downHoldRemaining = 0;
                lastHz            = 0;
                svcSleepThread(POLL_NS);
                continue;
            }

            u32 mode = 0;
            Result rc = apmExtGetCurrentPerformanceConfiguration(&mode);

            if (R_SUCCEEDED(rc) && apmExtIsBoostMode(mode)) {
                isCpuGovernorInBoostMode = true;
                downHoldRemaining        = 0;
                lastHz                   = 0;
                continue; // TODO: figure out a way to get boost clock easily and set it instead of just skipping the governor
            } else if (!apmExtIsBoostMode(mode)) {
                isCpuGovernorInBoostMode = false;
            }

            auto& table = clockManager::gFreqTable[SysClkModule_CPU];

            if (table.count == 0)
                continue;

            std::scoped_lock lock{clockManager::gContextMutex};

            u32 cpuLoad    = board::GetPartLoad(HocClkPartLoad_CPUMax);

            u32 tableMaxHz = table.list[table.count - 1];
            u32 desiredHz  = SchedutilTargetHz(cpuLoad, tableMaxHz);
            u32 targetHz   = ResolveTargetHz(SysClkModule_CPU);
            u32 maxHz      = clockManager::GetMaxAllowedHz(SysClkModule_CPU, clockManager::gContext.profile);

            if (targetHz && desiredHz > targetHz)
                desiredHz = targetHz;

            if (maxHz && desiredHz > maxHz)
                desiredHz = maxHz;

            u32 newHz = table.list[TableIndexForHz(table, desiredHz)];

            // ramp up fast, go down slow
            bool goingDown = (lastHz != 0) && (newHz < lastHz);

            if (!goingDown)
                downHoldRemaining = 0;
            else if (downHoldRemaining == 0)
                downHoldRemaining = DOWN_HOLD_TICKS;

            if (downHoldRemaining > 0)
                downHoldRemaining--;

            if (++tick > 50) {
                minHz = config::GetConfigValue(HorizonOCConfigValue_CpuGovernorMinimumFreq);
                tick = 0;
            }

            if (newHz < minHz)
                newHz = minHz;

            if ((!goingDown || (downHoldRemaining == 0)) && clockManager::IsAssignableHz(SysClkModule_CPU, newHz)) {
                board::SetHz(SysClkModule_CPU, newHz);
                clockManager::gContext.freqs[SysClkModule_CPU] = newHz;
                lastHz = newHz;
            }

            svcSleepThread(POLL_NS);
        }
    }

    void GovernorThread(void* arg)
    {
        (void)arg;

        u32 downHoldRemaining = 0;
        u32 lastHz            = 0;

        for (;;) {
            if (!clockManager::gRunning || !isGpuGovernorEnabled) {
                downHoldRemaining = 0;
                lastHz            = 0;
                svcSleepThread(POLL_NS);
                continue;
            }

            auto& table = clockManager::gFreqTable[SysClkModule_GPU];
            if (table.count == 0)
                continue;

            std::scoped_lock lock{clockManager::gContextMutex};

            u32 gpuLoad    = board::GetPartLoad(HocClkPartLoad_GPU);
            u32 tableMaxHz = table.list[table.count - 1];
            u32 desiredHz  = SchedutilTargetHz(gpuLoad, tableMaxHz);
            u32 targetHz   = ResolveTargetHz(SysClkModule_GPU);
            u32 maxHz      = clockManager::GetMaxAllowedHz(SysClkModule_GPU, clockManager::gContext.profile);

            if (targetHz && desiredHz > targetHz)
                desiredHz = targetHz;

            if (maxHz && desiredHz > maxHz)
                desiredHz = maxHz;

            u32 newHz = table.list[TableIndexForHz(table, desiredHz)];
            bool goingDown = (lastHz != 0) && (newHz < lastHz);

            if (!goingDown)
                downHoldRemaining = 0;
            else if (downHoldRemaining == 0)
                downHoldRemaining = DOWN_HOLD_TICKS;

            if (downHoldRemaining > 0)
                downHoldRemaining--;

            if ((!goingDown || (downHoldRemaining == 0)) && clockManager::IsAssignableHz(SysClkModule_GPU, newHz)) {
                board::SetHz(SysClkModule_GPU, newHz);
                clockManager::gContext.freqs[SysClkModule_GPU] = newHz;
                lastHz = newHz;
            }

            svcSleepThread(POLL_NS);
        }
    }

    void VRRThread(void* arg)
    {
        (void)arg;

        u8 tick = 0;
        for (;;) {
            if (!clockManager::gRunning || clockManager::gContext.profile == SysClkProfile_Docked || !isVRREnabled) {
                svcSleepThread(POLL_NS);
                continue;
            }

            std::scoped_lock lock{clockManager::gContextMutex};

            u8 fps;

            if (clockManager::gContext.isSaltyNXInstalled) {
                fps = integrations::GetSaltyNXFPS();
            } else {
                svcSleepThread(~0ULL); // effectively disable the thread if SaltyNX isn't installed, as there's no point in it running
                continue;
            }

            if (fps == 254) {
                svcSleepThread(POLL_NS);
                continue;
            }
            // if(appletGetFocusState() != AppletFocusState_InFocus) {
            //     board::ResetToStockDisplay();
            //     continue;
            // }

            u32 targetHz = clockManager::gContext.overrideFreqs[HorizonOCModule_Display];
            if (!targetHz) {
                targetHz = config::GetAutoClockHz(clockManager::gContext.applicationId, HorizonOCModule_Display, clockManager::gContext.profile, false);
                if (!targetHz)
                    targetHz = config::GetAutoClockHz(GLOBAL_PROFILE_ID, HorizonOCModule_Display, clockManager::gContext.profile, false);
            }

            u8 maxDisplay;
            if (targetHz) {
                maxDisplay = targetHz;
            } else {
                maxDisplay = 60; // don't assume display stuff!
            }

            u8 minDisplay = board::GetConsoleType() == HorizonOCConsoleType_Aula ? 45 : 40;
            if (maxDisplay == minDisplay)
                continue;

            if (fps >= minDisplay && fps <= maxDisplay) {
                board::SetHz(HorizonOCModule_Display, fps);
                clockManager::gContext.freqs[HorizonOCModule_Display] = fps;
                clockManager::gContext.realFreqs[HorizonOCModule_Display] = fps;
            } else {
                for (u32 i = 0; i < 10; i++) {
                    u32 compareHz = fps * i;
                    if (compareHz >= minDisplay && compareHz <= maxDisplay) {
                        board::SetHz(HorizonOCModule_Display, compareHz);
                        clockManager::gContext.freqs[HorizonOCModule_Display] = compareHz;
                        clockManager::gContext.realFreqs[HorizonOCModule_Display] = compareHz;
                        break;
                    }
                }
            }

            if (++tick > 50) {
                board::SetHz(HorizonOCModule_Display, maxDisplay);
                tick = 0;
                svcSleepThread(50'000'000);
            }

            svcSleepThread(POLL_NS);
        }
    }

    
    void startThreads() {

        threadCreate(
            &cpuGovernorTHREAD,
            CpuGovernorThread,
            nullptr,
            NULL,
            0x2000,
            0x3F,
            -2
        );

        threadCreate(
            &gpuGovernorTHREAD,
            GovernorThread,
            nullptr,
            NULL,
            0x2000,
            0x3F,
            -2
        );

        threadCreate(
            &vrrTHREAD,
            VRRThread,
            nullptr,
            NULL,
            0x2000,
            0x3F,
            -2
        );

        threadStart(&cpuGovernorTHREAD);
        threadStart(&gpuGovernorTHREAD);
        threadStart(&vrrTHREAD);
    }

    void exitThreads() {
        threadClose(&cpuGovernorTHREAD);
        threadClose(&gpuGovernorTHREAD);
        threadClose(&vrrTHREAD);
    }
}