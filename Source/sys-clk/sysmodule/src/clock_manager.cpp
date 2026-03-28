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


#include "clock_manager.h"
#include <cstring>
#include "file_utils.h"
#include "board.h"
#include "process_management.h"
#include "errors.h"
#include "ipc_service.h"
#include "kip.h"
#include <i2c.h>
#include "notification.h"
#include <display_refresh_rate.h>
#include <cstring>
#include <cstdio>
#include <crc32.h>

#define HOSPPC_HAS_BOOST (hosversionAtLeast(7,0,0))

// governor constants
#define POLL_NS 5'000'000  // 5 ms  – governor poll rate
#define DOWN_HOLD_TICKS 10         // 50 ms – how long to in POLL_NS to hold while ramping down
#define STEP_UTIL 900        // multiplier for step calculations

bool isGpuGovernorEnabled = false;
bool isCpuGovernorEnabled = false;
bool lastGpuGovernorState = false;
bool lastCpuGovernorState = false;
bool lastVrrGovernorState = false;
bool hasChanged = true;
ClockManager *ClockManager::instance = NULL;
Thread cpuGovernorTHREAD;
Thread gpuGovernorTHREAD;
Thread vrrTHREAD;
u32 initialConfigValues[SysClkConfigValue_EnumMax]; // initial config. used for safety checks
bool kipAvailable = false;
bool isCpuGovernorInBoostMode = false;
bool isVRREnabled = false;

ClockManager *ClockManager::GetInstance()
{
    return instance;
}

void ClockManager::Exit()
{
    if (instance)
    {
        delete instance;
    }
}

void ClockManager::Initialize()
{
    if (!instance)
    {
        instance = new ClockManager();
    }
}

ClockManager::ClockManager()
{
    this->config = Config::CreateDefault();
    this->context = new SysClkContext;
    this->context->applicationId = 0;
    this->context->profile = SysClkProfile_Handheld;
    for (unsigned int module = 0; module < SysClkModule_EnumMax; module++)
    {
        this->context->freqs[module] = 0;
        this->context->realFreqs[module] = 0;
        this->context->overrideFreqs[module] = 0;
        this->RefreshFreqTableRow((SysClkModule)module);
    }

    this->running = false;
    this->lastTempLogNs = 0;
    this->lastCsvWriteNs = 0;

    this->sysDockIntegration = new SysDockIntegration;
    this->saltyNXIntegration = new SaltyNXIntegration;

    memset(&initialConfigValues, 0, sizeof(initialConfigValues));
    this->GetKipData();

    threadCreate(
        &cpuGovernorTHREAD,
        ClockManager::CpuGovernorThread,
        this,
        NULL,
        0x2000,
        0x3F,
        -2
    );

    threadCreate(
        &gpuGovernorTHREAD,
        ClockManager::GovernorThread,
        this,
        NULL,
        0x2000,
        0x3F,
        -2
    );

    threadCreate(
        &vrrTHREAD,
        ClockManager::VRRThread,
        this,
        NULL,
        0x2000,
        0x3F,
        -2
    );

    for(int i = 0; i < HorizonOCSpeedo_EnumMax; i++) {
        this->context->speedos[i] = Board::getSpeedo((HorizonOCSpeedo)i);
        this->context->iddq[i] = Board::getIDDQ((HorizonOCSpeedo)i);
    }

    this->context->dramID = Board::GetDramID();
    this->context->isDram8GB = Board::IsDram8GB();
    Board::SetGpuSchedulingMode((GpuSchedulingMode)this->config->GetConfigValue(HorizonOCConfigValue_GPUScheduling), (GpuSchedulingOverrideMethod)this->config->GetConfigValue(HorizonOCConfigValue_GPUSchedulingMethod));
    this->context->gpuSchedulingMode = (GpuSchedulingMode)this->config->GetConfigValue(HorizonOCConfigValue_GPUScheduling);

    this->context->isSysDockInstalled = this->sysDockIntegration->getCurrentSysDockState();
    this->context->isSaltyNXInstalled = this->saltyNXIntegration->getCurrentSaltyNXState();
    if(this->context->isSaltyNXInstalled) {
        this->saltyNXIntegration->LoadSaltyNX();
    }

    this->context->isUsingRetroSuper = Board::IsUsingRetroSuperDisplay();
    Board::GetWaferPosition(&this->context->waferX, &this->context->waferY);
	threadStart(&cpuGovernorTHREAD);
	threadStart(&gpuGovernorTHREAD);
    threadStart(&vrrTHREAD);
}

ClockManager::~ClockManager()
{
    threadClose(&cpuGovernorTHREAD);
    threadClose(&gpuGovernorTHREAD);
    threadClose(&vrrTHREAD);

    delete this->sysDockIntegration;
    delete this->saltyNXIntegration;
    delete this->config;
    delete this->context;
}

SysClkContext ClockManager::GetCurrentContext()
{
    std::scoped_lock lock{this->contextMutex};
    return *this->context;
}

Config *ClockManager::GetConfig()
{
    return this->config;
}

void ClockManager::SetRunning(bool running)
{
    this->running = running;
}

bool ClockManager::Running()
{
    return this->running;
}

void ClockManager::GetFreqList(SysClkModule module, std::uint32_t *list, std::uint32_t maxCount, std::uint32_t *outCount)
{
    ASSERT_ENUM_VALID(SysClkModule, module);

    *outCount = std::min(maxCount, this->freqTable[module].count);
    memcpy(list, &this->freqTable[module].list[0], *outCount * sizeof(this->freqTable[0].list[0]));
}

bool ClockManager::IsAssignableHz(SysClkModule module, std::uint32_t hz)
{
    switch (module)
    {
    case SysClkModule_CPU:
        return hz >= 500000000;
    case SysClkModule_MEM:
        return hz >= 665600000;
    default:
        return true;
    }
}

std::uint32_t ClockManager::GetMaxAllowedHz(SysClkModule module, SysClkProfile profile)
{
    if (this->config->GetConfigValue(HocClkConfigValue_UncappedClocks))
    {
        return ~0; // Integer limit, uncapped clocks ON
    }
    else
    {
        if (module == SysClkModule_GPU)
        {
            if (profile < SysClkProfile_HandheldCharging)
            {
                switch(Board::GetSocType()) {
                    case SysClkSocType_Erista:
                        return 460800000;
                    case SysClkSocType_Mariko:
                        switch(this->config->GetConfigValue(KipConfigValue_marikoGpuUV)) {
                            case 0:
                                return 614400000;
                            case 1:
                                return 691200000;
                            case 2:
                                return 768000000;
                            default:
                                return 614400000;
                        }
                    default:
                        return 460800000;
                }
            }
            else if (profile <= SysClkProfile_HandheldChargingUSB)
            {
                switch(Board::GetSocType()) {
                    case SysClkSocType_Erista:
                        return 768000000;
                    case SysClkSocType_Mariko:
                        switch(this->config->GetConfigValue(KipConfigValue_marikoGpuUV)) {
                            case 0:
                                return 844800000;
                            case 1:
                                return 921600000;
                            case 2:
                                return 998400000;
                            default:
                                return 844800000;
                        }
                    default:
                        return 768000000;
                }
            }
        } else if(module == SysClkModule_CPU) {
            if(profile < SysClkProfile_HandheldCharging && Board::GetSocType() == SysClkSocType_Erista) {
                return 1581000000;
            } else {
                return ~0;
            }
        }
    }
    return 0;
}

std::uint32_t ClockManager::GetNearestHz(SysClkModule module, std::uint32_t inHz, std::uint32_t maxHz)
{
    std::uint32_t *freqs = &this->freqTable[module].list[0];
    size_t count = this->freqTable[module].count - 1;

    size_t i = 0;
    while (i < count)
    {
        if (maxHz > 0 && freqs[i] >= maxHz)
        {
            break;
        }

        if (inHz <= ((std::uint64_t)freqs[i] + freqs[i + 1]) / 2)
        {
            break;
        }

        i++;
    }

    return freqs[i];
}

bool ClockManager::ConfigIntervalTimeout(SysClkConfigValue intervalMsConfigValue, std::uint64_t ns, std::uint64_t *lastLogNs)
{
    std::uint64_t logInterval = this->GetConfig()->GetConfigValue(intervalMsConfigValue) * 1000000ULL;
    bool shouldLog = logInterval && ((ns - *lastLogNs) > logInterval);

    if (shouldLog)
    {
        *lastLogNs = ns;
    }

    return shouldLog;
}

void ClockManager::RefreshFreqTableRow(SysClkModule module)
{
    std::scoped_lock lock{this->contextMutex};

    std::uint32_t freqs[SYSCLK_FREQ_LIST_MAX];
    std::uint32_t count;

    FileUtils::LogLine("[mgr] %s freq list refresh", Board::GetModuleName(module, true));
    Board::GetFreqList(module, &freqs[0], SYSCLK_FREQ_LIST_MAX, &count);

    std::uint32_t *hz = &this->freqTable[module].list[0];
    this->freqTable[module].count = 0;
    for (std::uint32_t i = 0; i < count; i++)
    {
        if (!this->IsAssignableHz(module, freqs[i]))
        {
            continue;
        }

        *hz = freqs[i];
        FileUtils::LogLine("[mgr] %02u - %u - %u.%u MHz", this->freqTable[module].count, *hz, *hz / 1000000, *hz / 100000 - *hz / 1000000 * 10);

        this->freqTable[module].count++;
        hz++;
    }

    FileUtils::LogLine("[mgr] count = %u", this->freqTable[module].count);
}

u32 ClockManager::SchedutilTargetHz(u32 util, u32 tableMaxHz) {
    u64 hz = (u64)tableMaxHz * util / STEP_UTIL;
    return (u32)(std::min(hz, static_cast<u64>(tableMaxHz)));
}

u32 ClockManager::TableIndexForHz(const FreqTable& table, u32 targetHz) { // must pass in a freqTable as tables are different for cpu/gpu
    for (u32 i = 0; i < table.count; i++)
        if (table.list[i] >= targetHz)
            return i;
    return table.count - 1;
}

u32 ClockManager::ResolveTargetHz(ClockManager* mgr, SysClkModule module) {
    u32 hz = mgr->context->overrideFreqs[module];
    if (!hz)
        hz = mgr->config->GetAutoClockHz(
                mgr->context->applicationId, module,
                mgr->context->profile, false);
    if (!hz)
        hz = mgr->config->GetAutoClockHz(
                GLOBAL_PROFILE_ID, module,
                mgr->context->profile, false);
    return hz;
}

void ClockManager::CpuGovernorThread(void* arg) {
    ClockManager* mgr = static_cast<ClockManager*>(arg);

    u32 downHoldRemaining = 0;
    u32 lastHz            = 0;
    u32 minHz = 612;
    u32 tick = 0;
    for (;;) {
        if (!mgr->running || !isCpuGovernorEnabled) {
            downHoldRemaining = 0;
            lastHz            = 0;
            svcSleepThread(POLL_NS);
            continue;
        }

        u32 mode = 0;
        Result rc   = apmExtGetCurrentPerformanceConfiguration(&mode);

        if (R_SUCCEEDED(rc) && apmExtIsBoostMode(mode)) {
            isCpuGovernorInBoostMode = true;
            downHoldRemaining        = 0;
            lastHz                   = 0;
            continue; // TODO: figure out a way to get boost clock easily and set it instead of just skipping the governor
        } else if(!apmExtIsBoostMode(mode)) {
            isCpuGovernorInBoostMode = false;
        }

        auto& table = mgr->freqTable[SysClkModule_CPU];

        if (table.count == 0)
            continue;

        std::scoped_lock lock{mgr->contextMutex};

        u32 cpuLoad    = Board::GetPartLoad(HocClkPartLoad_CPUMax);

        u32 tableMaxHz = table.list[table.count - 1];
        u32 desiredHz  = ClockManager::SchedutilTargetHz(cpuLoad, tableMaxHz);
        u32 targetHz = ClockManager::ResolveTargetHz(mgr, SysClkModule_CPU);
        u32 maxHz = mgr->GetMaxAllowedHz(SysClkModule_CPU, mgr->context->profile);

        if (targetHz && desiredHz > targetHz)
            desiredHz = targetHz;

        if (maxHz && desiredHz > maxHz)
            desiredHz = maxHz;

        u32 newHz = table.list[ClockManager::TableIndexForHz(table, desiredHz)];

        // ramp up fast, go down slow
        bool goingDown = (lastHz != 0) && (newHz < lastHz);

        if (!goingDown)
            downHoldRemaining = 0;
        else if (downHoldRemaining == 0)
            downHoldRemaining = DOWN_HOLD_TICKS;

        if (downHoldRemaining > 0)
            downHoldRemaining--;

        if(++tick > 50) {
            minHz = mgr->config->GetConfigValue(HorizonOCConfigValue_CpuGovernorMinimumFreq);
            tick = 0;
        }

        if(newHz < minHz)
            newHz = minHz;

        if ((!goingDown || (downHoldRemaining == 0)) && mgr->IsAssignableHz(SysClkModule_CPU, newHz)) {
            Board::SetHz(SysClkModule_CPU, newHz);
            mgr->context->freqs[SysClkModule_CPU] = newHz;
            lastHz = newHz;
        }

        svcSleepThread(POLL_NS);
    }
}

void ClockManager::GovernorThread(void* arg) {
    ClockManager* mgr = static_cast<ClockManager*>(arg);

    u32 downHoldRemaining = 0;
    u32 lastHz            = 0;

    for (;;) {
        if (!mgr->running || !isGpuGovernorEnabled) {
            downHoldRemaining = 0;
            lastHz            = 0;
            svcSleepThread(POLL_NS);
            continue;
        }

        auto& table = mgr->freqTable[SysClkModule_GPU];
        if (table.count == 0)
            continue;

        std::scoped_lock lock{mgr->contextMutex};

        u32 gpuLoad    = Board::GetPartLoad(HocClkPartLoad_GPU);
        u32 tableMaxHz = table.list[table.count - 1];
        u32 desiredHz  = ClockManager::SchedutilTargetHz(gpuLoad, tableMaxHz);
        u32 targetHz = ClockManager::ResolveTargetHz(mgr, SysClkModule_GPU);
        u32 maxHz = mgr->GetMaxAllowedHz(SysClkModule_GPU, mgr->context->profile);

        if (targetHz && desiredHz > targetHz)
            desiredHz = targetHz;

        if (maxHz && desiredHz > maxHz)
            desiredHz = maxHz;

        u32 newHz = table.list[ClockManager::TableIndexForHz(table, desiredHz)];
        bool goingDown = (lastHz != 0) && (newHz < lastHz);

        if (!goingDown)
            downHoldRemaining = 0;
        else if (downHoldRemaining == 0)
            downHoldRemaining = DOWN_HOLD_TICKS;

        if (downHoldRemaining > 0)
            downHoldRemaining--;

        if ((!goingDown || (downHoldRemaining == 0)) && mgr->IsAssignableHz(SysClkModule_GPU, newHz)) {
            Board::SetHz(SysClkModule_GPU, newHz);
            mgr->context->freqs[SysClkModule_GPU] = newHz;
            lastHz = newHz;
        }

        svcSleepThread(POLL_NS);
    }
}

void ClockManager::VRRThread(void* arg) {
    ClockManager* mgr = static_cast<ClockManager*>(arg);
    u8 tick = 0;
    for (;;) {
        if (!mgr->running || mgr->context->profile == SysClkProfile_Docked || !isVRREnabled) {
            svcSleepThread(POLL_NS);
            continue;
        }

        std::scoped_lock lock{mgr->contextMutex};

        u8 fps;

        if(mgr->context->isSaltyNXInstalled) {
            fps = mgr->saltyNXIntegration->GetFPS();
        } else {
            svcSleepThread(~0ULL); // effectively disable the thread if SaltyNX isn't installed, as there's no point in it running
            continue;
        }


        if(fps == 254) {
            svcSleepThread(POLL_NS);
            continue;
        }
        // if(appletGetFocusState() != AppletFocusState_InFocus) {
        //     Board::ResetToStockDisplay();
        //     continue;
        // }

        u32 targetHz = mgr->context->overrideFreqs[HorizonOCModule_Display];
        if (!targetHz)
        {
            targetHz = mgr->config->GetAutoClockHz(mgr->context->applicationId, HorizonOCModule_Display, mgr->context->profile, false);
            if(!targetHz)
                targetHz = mgr->config->GetAutoClockHz(GLOBAL_PROFILE_ID, HorizonOCModule_Display, mgr->context->profile, false);
        }

        u8 maxDisplay;
        if(targetHz) {
            maxDisplay = targetHz;
        } else {
            maxDisplay = 60; // don't assume display stuff!
        }

        u8 minDisplay = Board::GetConsoleType() == HorizonOCConsoleType_Aula ? 45 : 40;
        if(maxDisplay == minDisplay)
            continue;

        if(fps >= minDisplay && fps <= maxDisplay) {
            Board::SetHz(HorizonOCModule_Display, fps);
            mgr->context->freqs[HorizonOCModule_Display] = fps;
            mgr->context->realFreqs[HorizonOCModule_Display] = fps;
        } else {
            for(u32 i = 0; i < 10; i++) {
                u32 compareHz = fps * i;
                if(compareHz >= minDisplay && compareHz <= maxDisplay) {
                    Board::SetHz(HorizonOCModule_Display, compareHz);
                    mgr->context->freqs[HorizonOCModule_Display] = compareHz;
                    mgr->context->realFreqs[HorizonOCModule_Display] = compareHz;
                    break;
                }
            }
        }


        if(++tick > 50) {
            Board::SetHz(HorizonOCModule_Display, maxDisplay);
            tick = 0;
            svcSleepThread(50'000'000);
        }

        svcSleepThread(POLL_NS);
    }
}

void ClockManager::HandleSafetyFeatures() {
    if(this->config->GetConfigValue(HocClkConfigValue_HandheldTDP) && (this->context->profile != SysClkProfile_Docked)) { // Enable while charging as non-PD charger can cause lack of power
        if(Board::GetConsoleType() == HorizonOCConsoleType_Hoag) {
            if(Board::GetPowerMw(SysClkPowerSensor_Avg) < -(int)this->config->GetConfigValue(HocClkConfigValue_LiteTDPLimit)) {
                ResetToStockClocks();
                return;
            }
        } else {
            if(Board::GetPowerMw(SysClkPowerSensor_Avg) < -(int)this->config->GetConfigValue(HocClkConfigValue_HandheldTDPLimit)) {
                ResetToStockClocks();
                return;
            }
        }
    }

    if(((tmp451TempSoc() / 1000) > (int)this->config->GetConfigValue(HocClkConfigValue_ThermalThrottleThreshold)) && this->config->GetConfigValue(HocClkConfigValue_ThermalThrottle)) {
        ResetToStockClocks();
        return;
    }
}

void ClockManager::HandleMiscFeatures() {
    static u32 prevBrightness = 100;
    static bool wasPWMDimEnabled = false;
    if(Board::GetConsoleType() == HorizonOCConsoleType_Aula && this->config->GetConfigValue(HorizonOCConfigValue_PWMDimming)) {
        float out = 1.0;
        Result rc = lblGetCurrentBrightnessSetting(&out);
        ASSERT_RESULT_OK(rc, "lblGetCurrentBrightnessSetting");
        u32 brightness = (u32)(out * 100);
        Board::SetPWMDimEnabled(true);
        Board::SetPWMDimBrightness(prevBrightness, brightness, true);
        prevBrightness = brightness;
        wasPWMDimEnabled = true;
    } else if (Board::GetConsoleType() == HorizonOCConsoleType_Aula && wasPWMDimEnabled) {
        Board::SetPWMDimEnabled(false);
        Board::SetPWMDimBrightness(0, 0, false);
        float out = 1.0;
        Result rc = lblGetCurrentBrightnessSetting(&out);
        ASSERT_RESULT_OK(rc, "lblGetCurrentBrightnessSetting");
        rc = lblSetCurrentBrightnessSetting(out);
        ASSERT_RESULT_OK(rc, "lblSetCurrentBrightnessSetting");
        wasPWMDimEnabled = false;
    }

    if(this->config->GetConfigValue(HorizonOCConfigValue_BatteryChargeCurrent)) {
        I2c_Bq24193_SetFastChargeCurrentLimit(this->config->GetConfigValue(HorizonOCConfigValue_BatteryChargeCurrent));
    }
}

void ClockManager::HandleGovernor(uint32_t targetHz) {
    u32 tempTargetHz = this->context->overrideFreqs[HorizonOCModule_Governor];
    if (!tempTargetHz)
    {
        tempTargetHz = this->config->GetAutoClockHz(this->context->applicationId, HorizonOCModule_Governor, this->context->profile, true);
        if (!tempTargetHz)
            tempTargetHz = this->config->GetAutoClockHz(GLOBAL_PROFILE_ID, HorizonOCModule_Governor, this->context->profile, true);
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

    if(newCpuGovernorState == false && lastCpuGovernorState == true) {
        svcSleepThread(100'000'000); // thread syncing. probably a cleaner way to do this but hey, it works!
        Board::ResetToStockCpu();
    }
    if(newGpuGovernorState == false && lastGpuGovernorState == true) {
        svcSleepThread(100'000'000);
        Board::ResetToStockGpu();
    }
    if (newVrrGovernorState == false && lastVrrGovernorState == true) {
        svcSleepThread(100'000'000);
        Board::ResetToStockDisplay();
    }
    if(newCpuGovernorState != lastCpuGovernorState || newGpuGovernorState != lastGpuGovernorState || newVrrGovernorState != lastVrrGovernorState) {
        FileUtils::LogLine("[mgr] Governor state changed: CPU %s, GPU %s, VRR %s", newCpuGovernorState ? "enabled" : "disabled", newGpuGovernorState ? "enabled" : "disabled", newVrrGovernorState ? "enabled" : "disabled");
        lastCpuGovernorState = newCpuGovernorState;
        lastGpuGovernorState = newGpuGovernorState;
        lastVrrGovernorState = newVrrGovernorState;
    }
}

void ClockManager::DVFSBeforeSet(u32 targetHz) {
    s32 dvfsOffset = this->config->GetConfigValue(HorizonOCConfigValue_DVFSOffset);
    u32 vmin = Board::GetMinimumGpuVoltage(targetHz / 1000000) + dvfsOffset;

    Board::PcvHijackDvfs(vmin);

    /* Update the voltage. */
    if (I2c_BuckConverter_GetMvOut(&I2c_Mariko_GPU) < vmin) {
        I2c_BuckConverter_SetMvOut(&I2c_Mariko_GPU, vmin);
    }

    this->context->voltages[HocClkVoltage_GPU] = vmin * 1000;
}

void ClockManager::DVFSAfterSet(u32 targetHz) {
    s32 dvfsOffset = this->config->GetConfigValue(HorizonOCConfigValue_DVFSOffset);
    dvfsOffset = std::max(dvfsOffset, -80);
    u32 vmin = Board::GetMinimumGpuVoltage(targetHz / 1000000);

    if (vmin) {
        vmin += dvfsOffset;
    }

    u32 maxHz = this->GetMaxAllowedHz(SysClkModule_GPU, this->context->profile);
    u32 nearestHz = this->GetNearestHz(SysClkModule_GPU, targetHz, maxHz);
    Board::PcvHijackDvfs(vmin);

    if (targetHz) {
        Board::SetHz(SysClkModule_GPU, ~0);
        Board::SetHz(SysClkModule_GPU, nearestHz);
    } else {
        Board::SetHz(SysClkModule_GPU, ~0);
        Board::ResetToStockGpu();
    }
}

void ClockManager::HandleCpuUv() {
    if(Board::GetSocType() == SysClkSocType_Erista)
        Board::SetCpuUvLevel(this->config->GetConfigValue(KipConfigValue_eristaCpuUV), 0, 1581000000);
    else
        Board::SetCpuUvLevel(this->config->GetConfigValue(KipConfigValue_marikoCpuUVLow), this->config->GetConfigValue(KipConfigValue_marikoCpuUVHigh), Board::CalculateTbreak(this->config->GetConfigValue(KipConfigValue_tableConf)));
}

void ClockManager::DVFSReset() {
    if (Board::GetSocType() == SysClkSocType_Mariko && this->config->GetConfigValue(HorizonOCConfigValue_DVFSMode) == DVFSMode_Hijack) {
        Board::PcvHijackDvfs(0);

        u32 targetHz = this->context->overrideFreqs[SysClkModule_GPU];
        if (!targetHz) {
            targetHz = this->config->GetAutoClockHz(this->context->applicationId, SysClkModule_GPU, this->context->profile, false);
            if(!targetHz) {
                targetHz = this->config->GetAutoClockHz(GLOBAL_PROFILE_ID, SysClkModule_GPU, this->context->profile, false);
            }
        }
        u32 maxHz = this->GetMaxAllowedHz(SysClkModule_GPU, this->context->profile);
        u32 nearestHz = this->GetNearestHz(SysClkModule_GPU, targetHz, maxHz);

        Board::SetHz(SysClkModule_GPU, ~0);
        if(targetHz) {
            Board::SetHz(SysClkModule_GPU, nearestHz);
        } else {
            Board::ResetToStockGpu();
        }
    }
}

void ClockManager::HandleFreqReset(SysClkModule module, bool isBoost) {
    switch (module)
    {
    case SysClkModule_CPU:
        if(!(isBoost || (this->config->GetConfigValue(HocClkConfigValue_OverwriteBoostMode) && isBoost)))
            Board::ResetToStockCpu();
        if(this->config->GetConfigValue(HorizonOCConfigValue_LiveCpuUv)) {
            if(Board::GetSocType() == SysClkSocType_Erista)
                Board::SetCpuUvLevel(this->config->GetConfigValue(KipConfigValue_eristaCpuUV), 0, 1581000000);
            else
                Board::SetCpuUvLevel(this->config->GetConfigValue(KipConfigValue_marikoCpuUVLow), this->config->GetConfigValue(KipConfigValue_marikoCpuUVHigh), Board::CalculateTbreak(this->config->GetConfigValue(KipConfigValue_tableConf)));
        }

        break;
    case SysClkModule_GPU:
        Board::ResetToStockGpu();
        break;
    case SysClkModule_MEM:
        Board::ResetToStockMem();
        DVFSReset();
        break;
    case HorizonOCModule_Display:
        if(this->config->GetConfigValue(HorizonOCConfigValue_OverwriteRefreshRate)) {
            Board::ResetToStockDisplay();
        }
        break;
    default:
        break;
    }

}

void ClockManager::SetClocks(bool isBoost) {
    std::uint32_t targetHz = 0;
    std::uint32_t maxHz = 0;
    std::uint32_t nearestHz = 0;

    if(isBoost && !this->config->GetConfigValue(HocClkConfigValue_OverwriteBoostMode)) {
        u32 boostFreq = Board::GetHz(SysClkModule_CPU);
        if (boostFreq / 1000000 > 1785) {
            Board::SetHz(SysClkModule_CPU, boostFreq);
        }
        return; // Return if we are't overwriting boost mode
    }

    bool returnRaw = false; // Return a value scaled to MHz instead of raw value
    for (unsigned int module = 0; module < SysClkModule_EnumMax; module++)
    {
        u32 oldHz = Board::GetHz((SysClkModule)module); // Get Old hz (used primarily for DVFS Logic)

        if(module > SysClkModule_MEM)
            returnRaw = true;
        else
            returnRaw = false;
        targetHz = this->context->overrideFreqs[module];
        if (!targetHz)
        {
            targetHz = this->config->GetAutoClockHz(this->context->applicationId, (SysClkModule)module, this->context->profile, returnRaw);
            if(!targetHz)
                targetHz = this->config->GetAutoClockHz(GLOBAL_PROFILE_ID, (SysClkModule)module, this->context->profile, returnRaw);
        }

        if(module == HorizonOCModule_Governor) {
            HandleGovernor(targetHz);
        }

        bool noCPU = isCpuGovernorEnabled;
        bool noGPU = isGpuGovernorEnabled;
        bool noDisp = isVRREnabled;
        if(noDisp && module == HorizonOCModule_Display)
            continue;

        if(module == HorizonOCModule_Display && this->config->GetConfigValue(HorizonOCConfigValue_OverwriteRefreshRate) && !noDisp) {
            if(targetHz) {
                Board::SetHz(HorizonOCModule_Display, targetHz);
                this->context->freqs[HorizonOCModule_Display] = targetHz;
                this->context->realFreqs[HorizonOCModule_Display] = targetHz;
            } else {
                HandleFreqReset(HorizonOCModule_Display, isBoost);
            }

        }

        // Skip GPU and CPU if governors handle them
        if(module > SysClkModule_MEM) {
            continue;
        }


        if(noCPU && module == SysClkModule_CPU)
            continue;
        if(noGPU && module == SysClkModule_GPU)
            continue;

        if (targetHz)
        {
            maxHz = this->GetMaxAllowedHz((SysClkModule)module, this->context->profile);
            nearestHz = this->GetNearestHz((SysClkModule)module, targetHz, maxHz);

            if (nearestHz != this->context->freqs[module]) {
                FileUtils::LogLine(
                    "[mgr] %s clock set : %u.%u MHz (target = %u.%u MHz)",
                    Board::GetModuleName((SysClkModule)module, true),
                    nearestHz / 1000000, nearestHz / 100000 - nearestHz / 1000000 * 10,
                    targetHz / 1000000, targetHz / 100000 - targetHz / 1000000 * 10
                );

                if(module == SysClkModule_MEM && Board::GetSocType() == SysClkSocType_Mariko && targetHz > oldHz && this->config->GetConfigValue(HorizonOCConfigValue_DVFSMode) == DVFSMode_Hijack) {
                    DVFSBeforeSet(targetHz);
                }

                Board::SetHz((SysClkModule)module, nearestHz);
                this->context->freqs[module] = nearestHz;

                if(module == SysClkModule_CPU && (this->config->GetConfigValue(HorizonOCConfigValue_LiveCpuUv))) {
                    HandleCpuUv();
                }

                if(module == SysClkModule_MEM && Board::GetSocType() == SysClkSocType_Mariko && targetHz < oldHz && this->config->GetConfigValue(HorizonOCConfigValue_DVFSMode) == DVFSMode_Hijack) {
                    DVFSAfterSet(targetHz);
                }
            }
        } else {
            HandleFreqReset((SysClkModule)module, isBoost);
        }
    }

}

void ClockManager::Tick()
{
    std::scoped_lock lock{this->contextMutex};
    std::uint32_t mode = 0;
    Result rc = apmExtGetCurrentPerformanceConfiguration(&mode);
    ASSERT_RESULT_OK(rc, "apmExtGetCurrentPerformanceConfiguration");

    bool isBoost = apmExtIsBoostMode(mode);

    HandleSafetyFeatures();

    if (this->RefreshContext() || this->config->Refresh())
    {
        HandleMiscFeatures();
        SetClocks(isBoost);
    }
}

void ClockManager::ResetToStockClocks() {
    Board::ResetToStockCpu();
    if(this->config->GetConfigValue(HorizonOCConfigValue_LiveCpuUv))
    {
        if(Board::GetSocType() == SysClkSocType_Erista)
            Board::SetCpuUvLevel(this->config->GetConfigValue(KipConfigValue_eristaCpuUV), 0, 1581000000);
        else
            Board::SetCpuUvLevel(this->config->GetConfigValue(KipConfigValue_marikoCpuUVLow), this->config->GetConfigValue(KipConfigValue_marikoCpuUVHigh), Board::CalculateTbreak(this->config->GetConfigValue(KipConfigValue_tableConf)));
    }

    Board::ResetToStockGpu();
}

void ClockManager::WaitForNextTick()
{
    if(!(Board::GetHz(SysClkModule_MEM) < 665000000))
        svcSleepThread(this->GetConfig()->GetConfigValue(SysClkConfigValue_PollingIntervalMs) * 1000000ULL);
    else
        svcSleepThread(5000 * 1000000ULL); // 5 seconds in sleep mode
}

bool ClockManager::RefreshContext()
{
    bool hasChanged = false;

    std::uint32_t mode = 0;
    Result rc = apmExtGetCurrentPerformanceConfiguration(&mode);
    ASSERT_RESULT_OK(rc, "apmExtGetCurrentPerformanceConfiguration");

    std::uint64_t applicationId = ProcessManagement::GetCurrentApplicationId();
    if (applicationId != this->context->applicationId)
    {
        FileUtils::LogLine("[mgr] TitleID change: %016lX", applicationId);
        this->context->applicationId = applicationId;
        hasChanged = true;
    }

    SysClkProfile profile = Board::GetProfile();
    if (profile != this->context->profile)
    {
        FileUtils::LogLine("[mgr] Profile change: %s", Board::GetProfileName(profile, true));
        this->context->profile = profile;
        hasChanged = true;
    }

    // restore clocks to stock values on app or profile change
    if (hasChanged)
    {
        Board::ResetToStock();
        if (Board::GetSocType() == SysClkSocType_Mariko && this->config->GetConfigValue(HorizonOCConfigValue_DVFSMode) == DVFSMode_Hijack) {
            Board::PcvHijackDvfs(0);
            Board::SetHz(SysClkModule_GPU, ~0);
            Board::ResetToStockGpu();
        }
        this->WaitForNextTick();
    }

    std::uint32_t hz = 0;
    for (unsigned int module = 0; module < SysClkModule_EnumMax; module++)
    {
        hz = Board::GetHz((SysClkModule)module);
        if (hz != 0 && hz != this->context->freqs[module])
        {
            FileUtils::LogLine("[mgr] %s clock change: %u.%u MHz", Board::GetModuleName((SysClkModule)module, true), hz / 1000000, hz / 100000 - hz / 1000000 * 10);
            this->context->freqs[module] = hz;
            hasChanged = true;
        }

        hz = this->GetConfig()->GetOverrideHz((SysClkModule)module);
        if (hz != this->context->overrideFreqs[module])
        {
            if (hz)
            {
                FileUtils::LogLine("[mgr] %s override change: %u.%u MHz", Board::GetModuleName((SysClkModule)module, true), hz / 1000000, hz / 100000 - hz / 1000000 * 10);
            }
            this->context->overrideFreqs[module] = hz;
            hasChanged = true;
        }
    }

    std::uint64_t ns = armTicksToNs(armGetSystemTick());

    // temperatures do not and should not force a refresh, hasChanged untouched
    std::uint32_t millis = 0;
    bool shouldLogTemp = this->ConfigIntervalTimeout(SysClkConfigValue_TempLogIntervalMs, ns, &this->lastTempLogNs);
    for (unsigned int sensor = 0; sensor < SysClkThermalSensor_EnumMax; sensor++)
    {
        millis = Board::GetTemperatureMilli((SysClkThermalSensor)sensor);
        if (shouldLogTemp)
        {
            FileUtils::LogLine("[mgr] %s temp: %u.%u °C", Board::GetThermalSensorName((SysClkThermalSensor)sensor, true), millis / 1000, (millis - millis / 1000 * 1000) / 100);
        }
        this->context->temps[sensor] = millis;
    }

    // power stats do not and should not force a refresh, hasChanged untouched
    std::int32_t mw = 0;
    bool shouldLogPower = this->ConfigIntervalTimeout(SysClkConfigValue_PowerLogIntervalMs, ns, &this->lastPowerLogNs);
    for (unsigned int sensor = 0; sensor < SysClkPowerSensor_EnumMax; sensor++)
    {
        mw = Board::GetPowerMw((SysClkPowerSensor)sensor);
        if (shouldLogPower)
        {
            FileUtils::LogLine("[mgr] Power %s: %d mW", Board::GetPowerSensorName((SysClkPowerSensor)sensor, false), mw);
        }
        this->context->power[sensor] = mw;
    }

    // real freqs do not and should not force a refresh, hasChanged untouched
    std::uint32_t realHz = 0;
    bool shouldLogFreq = this->ConfigIntervalTimeout(SysClkConfigValue_FreqLogIntervalMs, ns, &this->lastFreqLogNs);
    for (unsigned int module = 0; module < SysClkModule_EnumMax; module++)
    {
        realHz = Board::GetRealHz((SysClkModule)module);
        if (shouldLogFreq)
        {
            FileUtils::LogLine("[mgr] %s real freq: %u.%u MHz", Board::GetModuleName((SysClkModule)module, true), realHz / 1000000, realHz / 100000 - realHz / 1000000 * 10);
        }
        this->context->realFreqs[module] = realHz;
    }

    // ram load do not and should not force a refresh, hasChanged untouched
    for (unsigned int loadSource = 0; loadSource < SysClkPartLoad_EnumMax; loadSource++)
    {
        this->context->partLoad[loadSource] = Board::GetPartLoad((SysClkPartLoad)loadSource);
    }

    for (unsigned int voltageSource = 0; voltageSource < HocClkVoltage_EnumMax; voltageSource++)
    {
        this->context->voltages[voltageSource] = Board::GetVoltage((HocClkVoltage)voltageSource);
    }

    if (this->ConfigIntervalTimeout(SysClkConfigValue_CsvWriteIntervalMs, ns, &this->lastCsvWriteNs))
    {
        FileUtils::WriteContextToCsv(this->context);
    }

    // this->context->maxDisplayFreq = Board::GetHighestDockedDisplayRate();
    u32 targetHz = this->context->overrideFreqs[HorizonOCModule_Display];
    if (!targetHz)
    {
        targetHz = this->config->GetAutoClockHz(this->context->applicationId, HorizonOCModule_Display, this->context->profile, true);
        if(!targetHz)
            targetHz = this->config->GetAutoClockHz(GLOBAL_PROFILE_ID, HorizonOCModule_Display, this->context->profile, true);
    }

    if(targetHz && this->context->realFreqs[HorizonOCModule_Display] > targetHz && this->context->profile != SysClkProfile_Docked) {
        this->context->realFreqs[HorizonOCModule_Display] = targetHz; // clean up display real freqs, should probably be moved to the real freqs loop?
        hasChanged = true;
    }

    if(!Board::IsHoag())
        Board::SetDisplayRefreshDockedState(this->context->profile == SysClkProfile_Docked);
    if(this->context->isSaltyNXInstalled)
        this->context->fps = saltyNXIntegration->GetFPS();
    else
        this->context->fps = 254; // N/A

    if(this->context->isSaltyNXInstalled)
        this->context->resolutionHeight = saltyNXIntegration->GetResolutionHeight();
    else
        this->context->resolutionHeight = 0; // N/A

    return hasChanged;
}

void ClockManager::SetKipData() {
    // TODO: figure out if this REALLY causes issues (i doubt it)
    // if(Board::GetSocType() == SysClkSocType_Mariko) {
    //     if(R_FAILED(I2c_BuckConverter_SetMvOut(&I2c_Mariko_DRAM_VDDQ, this->config->GetConfigValue(KipConfigValue_marikoEmcVddqVolt) / 1000))) {
    //         FileUtils::LogLine("[clock_manager] Failed set i2c vddq");
    //         writeNotification("Horizon OC\nFailed to write I2C\nwhile setting vddq");
    //     }
    // }
    CustomizeTable table;
    FILE* fp;
    fp = fopen("sdmc:/atmosphere/kips/hoc.kip", "r");

    if (fp == NULL) {
        writeNotification("Horizon OC\nKip opening failed");
        kipAvailable = false;
        return;
    } else {
        kipAvailable = true;
        fclose(fp);
    }

    if (!cust_read_and_cache("sdmc:/atmosphere/kips/hoc.kip", &table)) {
        FileUtils::LogLine("[clock_manager] Failed to read KIP file");
        writeNotification("Horizon OC\nKip read failed");
        return;
    }

    CUST_WRITE_FIELD_BATCH(&table, custRev, this->config->GetConfigValue(KipConfigValue_custRev));
    // CUST_WRITE_FIELD_BATCH(&table, mtcConf, this->config->GetConfigValue(KipConfigValue_mtcConf));
    CUST_WRITE_FIELD_BATCH(&table, hpMode, this->config->GetConfigValue(KipConfigValue_hpMode));

    CUST_WRITE_FIELD_BATCH(&table, commonEmcMemVolt, this->config->GetConfigValue(KipConfigValue_commonEmcMemVolt));
    CUST_WRITE_FIELD_BATCH(&table, eristaEmcMaxClock, this->config->GetConfigValue(KipConfigValue_eristaEmcMaxClock));
    CUST_WRITE_FIELD_BATCH(&table, eristaEmcMaxClock1, this->config->GetConfigValue(KipConfigValue_eristaEmcMaxClock1));
    CUST_WRITE_FIELD_BATCH(&table, eristaEmcMaxClock2, this->config->GetConfigValue(KipConfigValue_eristaEmcMaxClock2));
    CUST_WRITE_FIELD_BATCH(&table, marikoEmcMaxClock, this->config->GetConfigValue(KipConfigValue_marikoEmcMaxClock));
    CUST_WRITE_FIELD_BATCH(&table, marikoEmcVddqVolt, this->config->GetConfigValue(KipConfigValue_marikoEmcVddqVolt));
    CUST_WRITE_FIELD_BATCH(&table, emcDvbShift, this->config->GetConfigValue(KipConfigValue_emcDvbShift));

    CUST_WRITE_FIELD_BATCH(&table, t1_tRCD, this->config->GetConfigValue(KipConfigValue_t1_tRCD));
    CUST_WRITE_FIELD_BATCH(&table, t2_tRP, this->config->GetConfigValue(KipConfigValue_t2_tRP));
    CUST_WRITE_FIELD_BATCH(&table, t3_tRAS, this->config->GetConfigValue(KipConfigValue_t3_tRAS));
    CUST_WRITE_FIELD_BATCH(&table, t4_tRRD, this->config->GetConfigValue(KipConfigValue_t4_tRRD));
    CUST_WRITE_FIELD_BATCH(&table, t5_tRFC, this->config->GetConfigValue(KipConfigValue_t5_tRFC));
    CUST_WRITE_FIELD_BATCH(&table, t6_tRTW, this->config->GetConfigValue(KipConfigValue_t6_tRTW));
    CUST_WRITE_FIELD_BATCH(&table, t7_tWTR, this->config->GetConfigValue(KipConfigValue_t7_tWTR));
    CUST_WRITE_FIELD_BATCH(&table, t8_tREFI, this->config->GetConfigValue(KipConfigValue_t8_tREFI));
    CUST_WRITE_FIELD_BATCH(&table, mem_burst_read_latency, this->config->GetConfigValue(KipConfigValue_mem_burst_read_latency));
    CUST_WRITE_FIELD_BATCH(&table, mem_burst_write_latency, this->config->GetConfigValue(KipConfigValue_mem_burst_write_latency));
    CUST_WRITE_FIELD_BATCH(&table, eristaCpuUV, this->config->GetConfigValue(KipConfigValue_eristaCpuUV));
    CUST_WRITE_FIELD_BATCH(&table, eristaCpuVmin, this->config->GetConfigValue(KipConfigValue_eristaCpuVmin));
    CUST_WRITE_FIELD_BATCH(&table, eristaCpuMaxVolt, this->config->GetConfigValue(KipConfigValue_eristaCpuMaxVolt));
    CUST_WRITE_FIELD_BATCH(&table, eristaCpuUnlock, this->config->GetConfigValue(KipConfigValue_eristaCpuUnlock));

    CUST_WRITE_FIELD_BATCH(&table, marikoCpuUVLow, this->config->GetConfigValue(KipConfigValue_marikoCpuUVLow));
    CUST_WRITE_FIELD_BATCH(&table, marikoCpuUVHigh, this->config->GetConfigValue(KipConfigValue_marikoCpuUVHigh));
    CUST_WRITE_FIELD_BATCH(&table, tableConf, this->config->GetConfigValue(KipConfigValue_tableConf));
    CUST_WRITE_FIELD_BATCH(&table, marikoCpuLowVmin, this->config->GetConfigValue(KipConfigValue_marikoCpuLowVmin));
    CUST_WRITE_FIELD_BATCH(&table, marikoCpuHighVmin, this->config->GetConfigValue(KipConfigValue_marikoCpuHighVmin));
    CUST_WRITE_FIELD_BATCH(&table, marikoCpuMaxVolt, this->config->GetConfigValue(KipConfigValue_marikoCpuMaxVolt));
    CUST_WRITE_FIELD_BATCH(&table, marikoCpuMaxClock, this->config->GetConfigValue(KipConfigValue_marikoCpuMaxClock));

    CUST_WRITE_FIELD_BATCH(&table, eristaCpuBoostClock, this->config->GetConfigValue(KipConfigValue_eristaCpuBoostClock));
    CUST_WRITE_FIELD_BATCH(&table, marikoCpuBoostClock, this->config->GetConfigValue(KipConfigValue_marikoCpuBoostClock));

    CUST_WRITE_FIELD_BATCH(&table, eristaGpuUV, this->config->GetConfigValue(KipConfigValue_eristaGpuUV));
    CUST_WRITE_FIELD_BATCH(&table, eristaGpuVmin, this->config->GetConfigValue(KipConfigValue_eristaGpuVmin));

    CUST_WRITE_FIELD_BATCH(&table, marikoGpuUV, this->config->GetConfigValue(KipConfigValue_marikoGpuUV));
    CUST_WRITE_FIELD_BATCH(&table, marikoGpuVmin, this->config->GetConfigValue(KipConfigValue_marikoGpuVmin));
    CUST_WRITE_FIELD_BATCH(&table, marikoGpuVmax, this->config->GetConfigValue(KipConfigValue_marikoGpuVmax));

    CUST_WRITE_FIELD_BATCH(&table, commonGpuVoltOffset, this->config->GetConfigValue(KipConfigValue_commonGpuVoltOffset));
    CUST_WRITE_FIELD_BATCH(&table, gpuSpeedo, this->config->GetConfigValue(KipConfigValue_gpuSpeedo));

    for (int i = 0; i < 24; i++) {
        table.marikoGpuVoltArray[i] = this->config->GetConfigValue((SysClkConfigValue)(KipConfigValue_g_volt_76800 + i));
    }

    for (int i = 0; i < 27; i++) {
        table.eristaGpuVoltArray[i] = this->config->GetConfigValue((SysClkConfigValue)(KipConfigValue_g_volt_e_76800 + i));
    }

    CUST_WRITE_FIELD_BATCH(&table, t6_tRTW_fine_tune, this->config->GetConfigValue(KipConfigValue_t6_tRTW_fine_tune));
    CUST_WRITE_FIELD_BATCH(&table, t7_tWTR_fine_tune, this->config->GetConfigValue(KipConfigValue_t7_tWTR_fine_tune));

    if (!cust_write_table("sdmc:/atmosphere/kips/hoc.kip", &table)) {
        FileUtils::LogLine("[clock_manager] Failed to write KIP file");
        writeNotification("Horizon OC\nKip write failed");
    }

    SysClkConfigValueList configValues;
    this->config->GetConfigValues(&configValues);

    configValues.values[KipCrc32] = (u64)checksum_file("sdmc:/atmosphere/kips/hoc.kip"); // write checksum

    if (this->config->SetConfigValues(&configValues, false)) {
        FileUtils::LogLine("[clock_manager] Successfully loaded KIP data into config");
    } else {
        FileUtils::LogLine("[clock_manager] Warning: Failed to set config values from KIP");
        writeNotification("Horizon OC\nKip config set failed");
    }
}

// I know this is very hacky, but the config system in the sysmodule doesn't really support writing

void ClockManager::GetKipData() {
    FILE* fp;
    if(this->config->Refresh()) {

        fp = fopen("sdmc:/atmosphere/kips/hoc.kip", "r");

        if (fp == NULL) {
            writeNotification("Horizon OC\nKip opening failed");
            kipAvailable = false;
            return;
        } else {
            kipAvailable = true;
            fclose(fp);
        }



        SysClkConfigValueList configValues;
        this->config->GetConfigValues(&configValues);

        CustomizeTable table;

        if (!cust_read_and_cache("sdmc:/atmosphere/kips/hoc.kip", &table)) {
            FileUtils::LogLine("[clock_manager] Failed to read KIP file for GetKipData");
            writeNotification("Horizon OC\nKip read failed");
            return;
        }

        if((u64)checksum_file("sdmc:/atmosphere/kips/hoc.kip") != this->config->GetConfigValue(KipCrc32) && !this->config->GetConfigValue(HocClkConfigValue_IsFirstLoad)) {
            SetKipData();
            writeNotification("Horizon OC\nKIP has been updated");
            writeNotification("Horizon OC\nPlease reboot your console");
            writeNotification("Horizon OC\nto complete the update");
            return;
        }
        if(this->config->GetConfigValue(HocClkConfigValue_IsFirstLoad) == true) {
            configValues.values[HocClkConfigValue_IsFirstLoad] = (u64)false;
            writeNotification("Horizon OC has been installed");
        }
        static bool writeBootConfigValues = true;

        configValues.values[KipCrc32] = (u64)checksum_file("sdmc:/atmosphere/kips/hoc.kip"); // write checksum


        if(writeBootConfigValues) {
            writeBootConfigValues = false;

            // initialConfigValues[KipConfigValue_mtcConf] = cust_get_mtc_conf(&table);
            initialConfigValues[KipConfigValue_hpMode] = cust_get_hp_mode(&table);

            initialConfigValues[KipConfigValue_commonEmcMemVolt] = cust_get_common_emc_volt(&table);
            initialConfigValues[KipConfigValue_eristaEmcMaxClock] = cust_get_erista_emc_max(&table);
            initialConfigValues[KipConfigValue_eristaEmcMaxClock1] = cust_get_erista_emc_max1(&table);
            initialConfigValues[KipConfigValue_eristaEmcMaxClock2] = cust_get_erista_emc_max2(&table);
            initialConfigValues[KipConfigValue_marikoEmcMaxClock] = cust_get_mariko_emc_max(&table);
            initialConfigValues[KipConfigValue_marikoEmcVddqVolt] = cust_get_mariko_emc_vddq(&table);
            initialConfigValues[KipConfigValue_emcDvbShift] = cust_get_emc_dvb_shift(&table);

            initialConfigValues[KipConfigValue_t1_tRCD] = cust_get_tRCD(&table);
            initialConfigValues[KipConfigValue_t2_tRP] = cust_get_tRP(&table);
            initialConfigValues[KipConfigValue_t3_tRAS] = cust_get_tRAS(&table);
            initialConfigValues[KipConfigValue_t4_tRRD] = cust_get_tRRD(&table);
            initialConfigValues[KipConfigValue_t5_tRFC] = cust_get_tRFC(&table);
            initialConfigValues[KipConfigValue_t6_tRTW] = cust_get_tRTW(&table);
            initialConfigValues[KipConfigValue_t7_tWTR] = cust_get_tWTR(&table);
            initialConfigValues[KipConfigValue_t8_tREFI] = cust_get_tREFI(&table);
            initialConfigValues[KipConfigValue_mem_burst_read_latency] = cust_get_burst_read_lat(&table);
            initialConfigValues[KipConfigValue_mem_burst_write_latency] = cust_get_burst_write_lat(&table);

            initialConfigValues[KipConfigValue_eristaCpuUV] = cust_get_erista_cpu_uv(&table);
            initialConfigValues[KipConfigValue_eristaCpuVmin] = cust_get_eristaCpuVmin(&table);
            initialConfigValues[KipConfigValue_eristaCpuMaxVolt] = cust_get_erista_cpu_max_volt(&table);
            initialConfigValues[KipConfigValue_eristaCpuUnlock] = cust_get_eristaCpuUnlock(&table);

            initialConfigValues[KipConfigValue_marikoCpuUVLow] = cust_get_mariko_cpu_uv_low(&table);
            initialConfigValues[KipConfigValue_marikoCpuUVHigh] = cust_get_mariko_cpu_uv_high(&table);
            initialConfigValues[KipConfigValue_tableConf] = cust_get_table_conf(&table);
            initialConfigValues[KipConfigValue_marikoCpuLowVmin] = cust_get_mariko_cpu_low_vmin(&table);
            initialConfigValues[KipConfigValue_marikoCpuHighVmin] = cust_get_mariko_cpu_high_vmin(&table);
            initialConfigValues[KipConfigValue_marikoCpuMaxVolt] = cust_get_mariko_cpu_max_volt(&table);
            initialConfigValues[KipConfigValue_marikoCpuMaxClock] = cust_get_marikoCpuMaxClock(&table);
            initialConfigValues[KipConfigValue_eristaCpuBoostClock] = cust_get_erista_cpu_boost(&table);
            initialConfigValues[KipConfigValue_marikoCpuBoostClock] = cust_get_mariko_cpu_boost(&table);

            initialConfigValues[KipConfigValue_eristaGpuUV] = cust_get_erista_gpu_uv(&table);
            initialConfigValues[KipConfigValue_eristaGpuVmin] = cust_get_erista_gpu_vmin(&table);
            initialConfigValues[KipConfigValue_marikoGpuUV] = cust_get_mariko_gpu_uv(&table);
            initialConfigValues[KipConfigValue_marikoGpuVmin] = cust_get_mariko_gpu_vmin(&table);
            initialConfigValues[KipConfigValue_marikoGpuVmax] = cust_get_mariko_gpu_vmax(&table);
            initialConfigValues[KipConfigValue_commonGpuVoltOffset] = cust_get_common_gpu_offset(&table);
            initialConfigValues[KipConfigValue_gpuSpeedo] = cust_get_gpu_speedo(&table);
            initialConfigValues[KipConfigValue_t6_tRTW_fine_tune] = cust_get_tRTW_fine_tune(&table);
            initialConfigValues[KipConfigValue_t7_tWTR_fine_tune] = cust_get_tWTR_fine_tune(&table);
        }

        // configValues.values[KipConfigValue_mtcConf] = cust_get_mtc_conf(&table);
        configValues.values[KipConfigValue_hpMode] = cust_get_hp_mode(&table);

        configValues.values[KipConfigValue_commonEmcMemVolt] = cust_get_common_emc_volt(&table);
        configValues.values[KipConfigValue_eristaEmcMaxClock] = cust_get_erista_emc_max(&table);
        configValues.values[KipConfigValue_eristaEmcMaxClock1] = cust_get_erista_emc_max1(&table);
        configValues.values[KipConfigValue_eristaEmcMaxClock2] = cust_get_erista_emc_max2(&table);
        configValues.values[KipConfigValue_marikoEmcMaxClock] = cust_get_mariko_emc_max(&table);
        configValues.values[KipConfigValue_marikoEmcVddqVolt] = cust_get_mariko_emc_vddq(&table);
        configValues.values[KipConfigValue_emcDvbShift] = cust_get_emc_dvb_shift(&table);

        configValues.values[KipConfigValue_t1_tRCD] = cust_get_tRCD(&table);
        configValues.values[KipConfigValue_t2_tRP] = cust_get_tRP(&table);
        configValues.values[KipConfigValue_t3_tRAS] = cust_get_tRAS(&table);
        configValues.values[KipConfigValue_t4_tRRD] = cust_get_tRRD(&table);
        configValues.values[KipConfigValue_t5_tRFC] = cust_get_tRFC(&table);
        configValues.values[KipConfigValue_t6_tRTW] = cust_get_tRTW(&table);
        configValues.values[KipConfigValue_t7_tWTR] = cust_get_tWTR(&table);
        configValues.values[KipConfigValue_t8_tREFI] = cust_get_tREFI(&table);
        configValues.values[KipConfigValue_mem_burst_read_latency] = cust_get_burst_read_lat(&table);
        configValues.values[KipConfigValue_mem_burst_write_latency] = cust_get_burst_write_lat(&table);

        configValues.values[KipConfigValue_eristaCpuUV] = cust_get_erista_cpu_uv(&table);
        configValues.values[KipConfigValue_eristaCpuVmin] = cust_get_eristaCpuVmin(&table);
        configValues.values[KipConfigValue_eristaCpuMaxVolt] = cust_get_erista_cpu_max_volt(&table);
        configValues.values[KipConfigValue_eristaCpuUnlock] = cust_get_eristaCpuUnlock(&table);


        configValues.values[KipConfigValue_marikoCpuUVLow] = cust_get_mariko_cpu_uv_low(&table);
        configValues.values[KipConfigValue_marikoCpuUVHigh] = cust_get_mariko_cpu_uv_high(&table);
        configValues.values[KipConfigValue_tableConf] = cust_get_table_conf(&table);
        configValues.values[KipConfigValue_marikoCpuLowVmin] = cust_get_mariko_cpu_low_vmin(&table);
        configValues.values[KipConfigValue_marikoCpuHighVmin] = cust_get_mariko_cpu_high_vmin(&table);
        configValues.values[KipConfigValue_marikoCpuMaxVolt] = cust_get_mariko_cpu_max_volt(&table);
        configValues.values[KipConfigValue_marikoCpuMaxClock] = cust_get_marikoCpuMaxClock(&table);
        configValues.values[KipConfigValue_eristaCpuBoostClock] = cust_get_erista_cpu_boost(&table);
        configValues.values[KipConfigValue_marikoCpuBoostClock] = cust_get_mariko_cpu_boost(&table);

        configValues.values[KipConfigValue_eristaGpuUV] = cust_get_erista_gpu_uv(&table);
        configValues.values[KipConfigValue_eristaGpuVmin] = cust_get_erista_gpu_vmin(&table);
        configValues.values[KipConfigValue_marikoGpuUV] = cust_get_mariko_gpu_uv(&table);
        configValues.values[KipConfigValue_marikoGpuVmin] = cust_get_mariko_gpu_vmin(&table);
        configValues.values[KipConfigValue_marikoGpuVmax] = cust_get_mariko_gpu_vmax(&table);
        configValues.values[KipConfigValue_commonGpuVoltOffset] = cust_get_common_gpu_offset(&table);
        configValues.values[KipConfigValue_gpuSpeedo] = Board::getSpeedo(HorizonOCSpeedo_GPU); // cust_get_gpu_speedo(&table);

        for (int i = 0; i < 24; i++) {
            configValues.values[KipConfigValue_g_volt_76800 + i] = cust_get_mariko_gpu_volt(&table, i);
            initialConfigValues[KipConfigValue_g_volt_76800 + i] = cust_get_mariko_gpu_volt(&table, i);
        }

        for (int i = 0; i < 27; i++) {
            configValues.values[KipConfigValue_g_volt_e_76800 + i] = cust_get_erista_gpu_volt(&table, i);
            initialConfigValues[KipConfigValue_g_volt_e_76800 + i] = cust_get_erista_gpu_volt(&table, i);
        }

        configValues.values[KipConfigValue_t7_tWTR_fine_tune] = cust_get_tWTR_fine_tune(&table);
        configValues.values[KipConfigValue_t6_tRTW_fine_tune] = cust_get_tRTW_fine_tune(&table);

        // if(cust_get_cust_rev(&table) == KIP_CUST_REV)
        //     return;

        if (sizeof(SysClkConfigValueList) <= sizeof(configValues)) {
            if (this->config->SetConfigValues(&configValues, false)) {
                FileUtils::LogLine("[clock_manager] Successfully loaded KIP data into config");
            } else {
                FileUtils::LogLine("[clock_manager] Warning: Failed to set config values from KIP");
                writeNotification("Horizon OC\nKip config set failed");
            }
        } else {
            FileUtils::LogLine("[clock_manager] Error: Config value list buffer size mismatch");
            writeNotification("Horizon OC\nConfig Buffer Mismatch");
        }
    } else {
        FileUtils::LogLine("[clock_manager] Config refresh error in GetKipData!");
        writeNotification("Horizon OC\nConfig refresh failed");
    }
}
