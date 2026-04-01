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

#include "clock_manager.hpp"
#include <cstring>
#include "file_utils.hpp"
#include "board/board.hpp"
#include "process_management.hpp"
#include "errors.hpp"
#include "ipc_service.hpp"
#include "kip.hpp"
#include <i2c.h>
#include "board/display_refresh_rate.hpp"
#include <cstdio>
#include <crc32.h>
#include "config.hpp"
#include "integrations.hpp"
#include <nxExt/cpp/lockable_mutex.h>
#include "kip.hpp"
#include "governor.hpp"

#define HOSPPC_HAS_BOOST (hosversionAtLeast(7,0,0))

namespace clockManager {


    bool gRunning = false;
    LockableMutex gContextMutex;
    SysClkContext gContext = {};
    FreqTable gFreqTable[SysClkModule_EnumMax];
    std::uint64_t gLastTempLogNs = 0;
    std::uint64_t gLastFreqLogNs = 0;
    std::uint64_t gLastPowerLogNs = 0;
    std::uint64_t gLastCsvWriteNs = 0;

    bool IsAssignableHz(SysClkModule module, std::uint32_t hz)
    {
        switch (module) {
        case SysClkModule_CPU:
            return hz >= 500000000;
        case SysClkModule_MEM:
            return hz >= 665600000;
        default:
            return true;
        }
    }

    std::uint32_t GetMaxAllowedHz(SysClkModule module, SysClkProfile profile)
    {
        if (config::GetConfigValue(HocClkConfigValue_UncappedClocks)) {
            return ~0; // Integer limit, uncapped clocks ON
        } else {
            if (module == SysClkModule_GPU) {
                if (profile < SysClkProfile_HandheldCharging) {
                    switch (board::GetSocType()) {
                    case SysClkSocType_Erista:
                        return 460800000;
                    case SysClkSocType_Mariko:
                        switch (config::GetConfigValue(KipConfigValue_marikoGpuUV)) {
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
                } else if (profile <= SysClkProfile_HandheldChargingUSB) {
                    switch (board::GetSocType()) {
                    case SysClkSocType_Erista:
                        return 768000000;
                    case SysClkSocType_Mariko:
                        switch (config::GetConfigValue(KipConfigValue_marikoGpuUV)) {
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
            } else if (module == SysClkModule_CPU) {
                if (profile < SysClkProfile_HandheldCharging && board::GetSocType() == SysClkSocType_Erista) {
                    return 1581000000;
                } else {
                    return ~0;
                }
            }
        }
        return 0;
    }

    std::uint32_t GetNearestHz(SysClkModule module, std::uint32_t inHz, std::uint32_t maxHz)
    {
        std::uint32_t *freqs = &gFreqTable[module].list[0];
        size_t count = gFreqTable[module].count - 1;

        size_t i = 0;
        while (i < count) {
            if (maxHz > 0 && freqs[i] >= maxHz) {
                break;
            }
            if (inHz <= ((std::uint64_t)freqs[i] + freqs[i + 1]) / 2) {
                break;
            }
            i++;
        }

        return freqs[i];
    }

    void ResetToStockClocks()
    {
        board::ResetToStockCpu();
        if (config::GetConfigValue(HorizonOCConfigValue_LiveCpuUv)) {
            if (board::GetSocType() == SysClkSocType_Erista)
                board::SetDfllTunings(config::GetConfigValue(KipConfigValue_eristaCpuUV), 0, 1581000000);
            else
                board::SetDfllTunings(config::GetConfigValue(KipConfigValue_marikoCpuUVLow), config::GetConfigValue(KipConfigValue_marikoCpuUVHigh), board::CalculateTbreak(config::GetConfigValue(KipConfigValue_tableConf)));
        }

        board::ResetToStockGpu();
    }

    bool ConfigIntervalTimeout(SysClkConfigValue intervalMsConfigValue, std::uint64_t ns, std::uint64_t *lastLogNs)
    {
        std::uint64_t logInterval = config::GetConfigValue(intervalMsConfigValue) * 1000000ULL;
        bool shouldLog = logInterval && ((ns - *lastLogNs) > logInterval);

        if (shouldLog) {
            *lastLogNs = ns;
        }

        return shouldLog;
    }

    void RefreshFreqTableRow(SysClkModule module)
    {
        std::scoped_lock lock{gContextMutex};

        std::uint32_t freqs[SYSCLK_FREQ_LIST_MAX];
        std::uint32_t count;

        fileUtils::LogLine("[mgr] %s freq list refresh", board::GetModuleName(module, true));
        board::GetFreqList(module, &freqs[0], SYSCLK_FREQ_LIST_MAX, &count);

        std::uint32_t *hz = &gFreqTable[module].list[0];
        gFreqTable[module].count = 0;
        for (std::uint32_t i = 0; i < count; i++) {
            if (!IsAssignableHz(module, freqs[i])) {
                continue;
            }

            *hz = freqs[i];
            fileUtils::LogLine("[mgr] %02u - %u - %u.%u MHz", gFreqTable[module].count, *hz, *hz / 1000000, *hz / 100000 - *hz / 1000000 * 10);

            gFreqTable[module].count++;
            hz++;
        }

        fileUtils::LogLine("[mgr] count = %u", gFreqTable[module].count);
    }

    void HandleSafetyFeatures()
    {
        if (config::GetConfigValue(HocClkConfigValue_HandheldTDP) && (gContext.profile != SysClkProfile_Docked)) {
            if (board::GetConsoleType() == HorizonOCConsoleType_Hoag) {
                if (board::GetPowerMw(SysClkPowerSensor_Avg) < -(int)config::GetConfigValue(HocClkConfigValue_LiteTDPLimit)) {
                    ResetToStockClocks();
                    return;
                }
            } else {
                if (board::GetPowerMw(SysClkPowerSensor_Avg) < -(int)config::GetConfigValue(HocClkConfigValue_HandheldTDPLimit)) {
                    ResetToStockClocks();
                    return;
                }
            }
        }

        if (((tmp451TempSoc() / 1000) > (int)config::GetConfigValue(HocClkConfigValue_ThermalThrottleThreshold)) && config::GetConfigValue(HocClkConfigValue_ThermalThrottle)) {
            ResetToStockClocks();
            return;
        }
    }

    void HandleMiscFeatures()
    {
        if (config::GetConfigValue(HorizonOCConfigValue_BatteryChargeCurrent)) {
            I2c_Bq24193_SetFastChargeCurrentLimit(config::GetConfigValue(HorizonOCConfigValue_BatteryChargeCurrent));
        }
    }

    void DVFSBeforeSet(u32 targetHz)
    {
        s32 dvfsOffset = config::GetConfigValue(HorizonOCConfigValue_DVFSOffset);
        u32 vmin = board::GetMinimumGpuVmin(targetHz / 1000000, board::GetGpuSpeedoBracket()) + dvfsOffset;

        board::PcvHijackGpuVolts(vmin);

        /* Update the voltage. */
        if (I2c_BuckConverter_GetMvOut(&I2c_Mariko_GPU) < vmin) {
            I2c_BuckConverter_SetMvOut(&I2c_Mariko_GPU, vmin);
        }

        gContext.voltages[HocClkVoltage_GPU] = vmin * 1000;
    }

    void DVFSAfterSet(u32 targetHz)
    {
        s32 dvfsOffset = config::GetConfigValue(HorizonOCConfigValue_DVFSOffset);
        dvfsOffset = std::max(dvfsOffset, -80);
        u32 vmin = board::GetMinimumGpuVmin(targetHz / 1000000, board::GetGpuSpeedoBracket());

        if (vmin) {
            vmin += dvfsOffset;
        }

        u32 maxHz = GetMaxAllowedHz(SysClkModule_GPU, gContext.profile);
        u32 nearestHz = GetNearestHz(SysClkModule_GPU, targetHz, maxHz);
        board::PcvHijackGpuVolts(vmin);

        if (targetHz) {
            board::SetHz(SysClkModule_GPU, ~0);
            board::SetHz(SysClkModule_GPU, nearestHz);
        } else {
            board::SetHz(SysClkModule_GPU, ~0);
            board::ResetToStockGpu();
        }
    }

    void HandleCpuUv()
    {
        if (board::GetSocType() == SysClkSocType_Erista)
            board::SetDfllTunings(config::GetConfigValue(KipConfigValue_eristaCpuUV), 0, 1581000000);
        else
            board::SetDfllTunings(config::GetConfigValue(KipConfigValue_marikoCpuUVLow), config::GetConfigValue(KipConfigValue_marikoCpuUVHigh), board::CalculateTbreak(config::GetConfigValue(KipConfigValue_tableConf)));
    }

    void DVFSReset()
    {
        if (board::GetSocType() == SysClkSocType_Mariko && config::GetConfigValue(HorizonOCConfigValue_DVFSMode) == DVFSMode_Hijack) {
            board::PcvHijackGpuVolts(0);

            u32 targetHz = gContext.overrideFreqs[SysClkModule_GPU];
            if (!targetHz) {
                targetHz = config::GetAutoClockHz(gContext.applicationId, SysClkModule_GPU, gContext.profile, false);
                if (!targetHz) {
                    targetHz = config::GetAutoClockHz(GLOBAL_PROFILE_ID, SysClkModule_GPU, gContext.profile, false);
                }
            }
            u32 maxHz = GetMaxAllowedHz(SysClkModule_GPU, gContext.profile);
            u32 nearestHz = GetNearestHz(SysClkModule_GPU, targetHz, maxHz);

            board::SetHz(SysClkModule_GPU, ~0);
            if (targetHz) {
                board::SetHz(SysClkModule_GPU, nearestHz);
            } else {
                board::ResetToStockGpu();
            }
        }
    }

    void HandleFreqReset(SysClkModule module, bool isBoost)
    {
        switch (module) {
        case SysClkModule_CPU:
            if (!(isBoost || (config::GetConfigValue(HocClkConfigValue_OverwriteBoostMode) && isBoost)))
                board::ResetToStockCpu();
            if (config::GetConfigValue(HorizonOCConfigValue_LiveCpuUv)) {
                if (board::GetSocType() == SysClkSocType_Erista)
                    board::SetDfllTunings(config::GetConfigValue(KipConfigValue_eristaCpuUV), 0, 1581000000);
                else
                    board::SetDfllTunings(config::GetConfigValue(KipConfigValue_marikoCpuUVLow), config::GetConfigValue(KipConfigValue_marikoCpuUVHigh), board::CalculateTbreak(config::GetConfigValue(KipConfigValue_tableConf)));
            }
            break;
        case SysClkModule_GPU:
            board::ResetToStockGpu();
            break;
        case SysClkModule_MEM:
            board::ResetToStockMem();
            DVFSReset();
            break;
        case HorizonOCModule_Display:
            if (config::GetConfigValue(HorizonOCConfigValue_OverwriteRefreshRate)) {
                board::ResetToStockDisplay();
            }
            break;
        default:
            break;
        }
    }

    void SetClocks(bool isBoost)
    {
        std::uint32_t targetHz = 0;
        std::uint32_t maxHz = 0;
        std::uint32_t nearestHz = 0;

        if (isBoost && !config::GetConfigValue(HocClkConfigValue_OverwriteBoostMode)) {
            u32 boostFreq = board::GetHz(SysClkModule_CPU);
            if (boostFreq / 1000000 > 1785) {
                board::SetHz(SysClkModule_CPU, boostFreq);
            }
            return; // Return if we aren't overwriting boost mode
        }

        bool returnRaw = false; // Return a value scaled to MHz instead of raw value
        for (unsigned int module = 0; module < SysClkModule_EnumMax; module++) {
            u32 oldHz = board::GetHz((SysClkModule)module); // Get Old hz (used primarily for DVFS Logic)

            if (module > SysClkModule_MEM)
                returnRaw = true;
            else
                returnRaw = false;
            targetHz = gContext.overrideFreqs[module];
            if (!targetHz) {
                targetHz = config::GetAutoClockHz(gContext.applicationId, (SysClkModule)module, gContext.profile, returnRaw);
                if (!targetHz)
                    targetHz = config::GetAutoClockHz(GLOBAL_PROFILE_ID, (SysClkModule)module, gContext.profile, returnRaw);
            }

            if (module == HorizonOCModule_Governor) {
                governor::HandleGovernor(targetHz);
            }

            bool noCPU = governor::isCpuGovernorEnabled;
            bool noGPU = governor::isGpuGovernorEnabled;
            bool noDisp = governor::isVRREnabled;
            if (noDisp && module == HorizonOCModule_Display)
                continue;

            if (module == HorizonOCModule_Display && config::GetConfigValue(HorizonOCConfigValue_OverwriteRefreshRate) && !noDisp) {
                if (targetHz) {
                    board::SetHz(HorizonOCModule_Display, targetHz);
                    gContext.freqs[HorizonOCModule_Display] = targetHz;
                    gContext.realFreqs[HorizonOCModule_Display] = targetHz;
                } else {
                    HandleFreqReset(HorizonOCModule_Display, isBoost);
                }
            }

            // Skip GPU and CPU if governors handle them
            if (module > SysClkModule_MEM) {
                continue;
            }

            if (noCPU && module == SysClkModule_CPU)
                continue;
            if (noGPU && module == SysClkModule_GPU)
                continue;

            if (targetHz) {
                maxHz = GetMaxAllowedHz((SysClkModule)module, gContext.profile);
                nearestHz = GetNearestHz((SysClkModule)module, targetHz, maxHz);

                if (nearestHz != gContext.freqs[module]) {
                    fileUtils::LogLine(
                        "[mgr] %s clock set : %u.%u MHz (target = %u.%u MHz)",
                        board::GetModuleName((SysClkModule)module, true),
                        nearestHz / 1000000, nearestHz / 100000 - nearestHz / 1000000 * 10,
                        targetHz / 1000000, targetHz / 100000 - targetHz / 1000000 * 10
                    );

                    if (module == SysClkModule_MEM && board::GetSocType() == SysClkSocType_Mariko && targetHz > oldHz && config::GetConfigValue(HorizonOCConfigValue_DVFSMode) == DVFSMode_Hijack) {
                        DVFSBeforeSet(targetHz);
                    }

                    board::SetHz((SysClkModule)module, nearestHz);
                    gContext.freqs[module] = nearestHz;

                    if (module == SysClkModule_CPU && config::GetConfigValue(HorizonOCConfigValue_LiveCpuUv)) {
                        HandleCpuUv();
                    }

                    if (module == SysClkModule_MEM && board::GetSocType() == SysClkSocType_Mariko && targetHz < oldHz && config::GetConfigValue(HorizonOCConfigValue_DVFSMode) == DVFSMode_Hijack) {
                        DVFSAfterSet(targetHz);
                    }
                }
            } else {
                HandleFreqReset((SysClkModule)module, isBoost);
            }
        }
    }

    bool RefreshContext()
    {
        bool hasChanged = false;

        std::uint32_t mode = 0;
        Result rc = apmExtGetCurrentPerformanceConfiguration(&mode);
        ASSERT_RESULT_OK(rc, "apmExtGetCurrentPerformanceConfiguration");

        std::uint64_t applicationId = processManagement::GetCurrentApplicationId();
        if (applicationId != gContext.applicationId) {
            fileUtils::LogLine("[mgr] TitleID change: %016lX", applicationId);
            gContext.applicationId = applicationId;
            hasChanged = true;
        }

        SysClkProfile profile = board::GetProfile();
        if (profile != gContext.profile) {
            fileUtils::LogLine("[mgr] Profile change: %s", board::GetProfileName(profile, true));
            gContext.profile = profile;
            hasChanged = true;
        }

        // restore clocks to stock values on app or profile change
        if (hasChanged) {
            board::ResetToStock();
            if (board::GetSocType() == SysClkSocType_Mariko && config::GetConfigValue(HorizonOCConfigValue_DVFSMode) == DVFSMode_Hijack) {
                board::PcvHijackGpuVolts(0);
                board::SetHz(SysClkModule_GPU, ~0);
                board::ResetToStockGpu();
            }
            WaitForNextTick();
        }

        std::uint32_t hz = 0;
        for (unsigned int module = 0; module < SysClkModule_EnumMax; module++) {
            hz = board::GetHz((SysClkModule)module);
            if (hz != 0 && hz != gContext.freqs[module]) {
                fileUtils::LogLine("[mgr] %s clock change: %u.%u MHz", board::GetModuleName((SysClkModule)module, true), hz / 1000000, hz / 100000 - hz / 1000000 * 10);
                gContext.freqs[module] = hz;
                hasChanged = true;
            }

            hz = config::GetOverrideHz((SysClkModule)module);
            if (hz != gContext.overrideFreqs[module]) {
                if (hz) {
                    fileUtils::LogLine("[mgr] %s override change: %u.%u MHz", board::GetModuleName((SysClkModule)module, true), hz / 1000000, hz / 100000 - hz / 1000000 * 10);
                }
                gContext.overrideFreqs[module] = hz;
                hasChanged = true;
            }
        }

        std::uint64_t ns = armTicksToNs(armGetSystemTick());

        // temperatures do not and should not force a refresh, hasChanged untouched
        std::uint32_t millis = 0;
        bool shouldLogTemp = ConfigIntervalTimeout(SysClkConfigValue_TempLogIntervalMs, ns, &gLastTempLogNs);
        for (unsigned int sensor = 0; sensor < SysClkThermalSensor_EnumMax; sensor++) {
            millis = board::GetTemperatureMilli((SysClkThermalSensor)sensor);
            if (shouldLogTemp) {
                fileUtils::LogLine("[mgr] %s temp: %u.%u °C", board::GetThermalSensorName((SysClkThermalSensor)sensor, true), millis / 1000, (millis - millis / 1000 * 1000) / 100);
            }
            gContext.temps[sensor] = millis;
        }

        // power stats do not and should not force a refresh, hasChanged untouched
        std::int32_t mw = 0;
        bool shouldLogPower = ConfigIntervalTimeout(SysClkConfigValue_PowerLogIntervalMs, ns, &gLastPowerLogNs);
        for (unsigned int sensor = 0; sensor < SysClkPowerSensor_EnumMax; sensor++) {
            mw = board::GetPowerMw((SysClkPowerSensor)sensor);
            if (shouldLogPower) {
                fileUtils::LogLine("[mgr] Power %s: %d mW", board::GetPowerSensorName((SysClkPowerSensor)sensor, false), mw);
            }
            gContext.power[sensor] = mw;
        }

        // real freqs do not and should not force a refresh, hasChanged untouched
        std::uint32_t realHz = 0;
        bool shouldLogFreq = ConfigIntervalTimeout(SysClkConfigValue_FreqLogIntervalMs, ns, &gLastFreqLogNs);
        for (unsigned int module = 0; module < SysClkModule_EnumMax; module++) {
            realHz = board::GetRealHz((SysClkModule)module);
            if (shouldLogFreq) {
                fileUtils::LogLine("[mgr] %s real freq: %u.%u MHz", board::GetModuleName((SysClkModule)module, true), realHz / 1000000, realHz / 100000 - realHz / 1000000 * 10);
            }
            gContext.realFreqs[module] = realHz;
        }

        // ram load do not and should not force a refresh, hasChanged untouched
        for (unsigned int loadSource = 0; loadSource < SysClkPartLoad_EnumMax; loadSource++) {
            gContext.partLoad[loadSource] = board::GetPartLoad((SysClkPartLoad)loadSource);
        }

        for (unsigned int voltageSource = 0; voltageSource < HocClkVoltage_EnumMax; voltageSource++) {
            gContext.voltages[voltageSource] = board::GetVoltage((HocClkVoltage)voltageSource);
        }

        if (ConfigIntervalTimeout(SysClkConfigValue_CsvWriteIntervalMs, ns, &gLastCsvWriteNs)) {
            fileUtils::WriteContextToCsv(&gContext);
        }

        // this->context->maxDisplayFreq = board::GetHighestDockedDisplayRate();
        u32 targetHz = gContext.overrideFreqs[HorizonOCModule_Display];
        if (!targetHz) {
            targetHz = config::GetAutoClockHz(gContext.applicationId, HorizonOCModule_Display, gContext.profile, true);
            if (!targetHz)
                targetHz = config::GetAutoClockHz(GLOBAL_PROFILE_ID, HorizonOCModule_Display, gContext.profile, true);
        }

        if (board::GetConsoleType() != HorizonOCConsoleType_Hoag)
            board::SetDisplayRefreshDockedState(gContext.profile == SysClkProfile_Docked);

        if (gContext.isSaltyNXInstalled)
            gContext.fps = integrations::GetSaltyNXFPS();
        else
            gContext.fps = 254; // N/A

        if (gContext.isSaltyNXInstalled)
            gContext.resolutionHeight = integrations::GetSaltyNXResolutionHeight();
        else
            gContext.resolutionHeight = 0; // N/A

        return hasChanged;
    }

    void Initialize()
    {
        config::Initialize();

        gContext = {};
        gContext.applicationId = 0;
        gContext.profile = SysClkProfile_Handheld;
        for (unsigned int module = 0; module < SysClkModule_EnumMax; module++) {
            gContext.freqs[module] = 0;
            gContext.realFreqs[module] = 0;
            gContext.overrideFreqs[module] = 0;
            RefreshFreqTableRow((SysClkModule)module);
        }

        gRunning = false;
        gLastTempLogNs = 0;
        gLastCsvWriteNs = 0;

        kip::GetKipData();

        board::FuseData *fuse = board::GetFuseData();

        gContext.speedos[0] = fuse->cpuSpeedo;
        gContext.speedos[1] = fuse->gpuSpeedo;
        gContext.speedos[2] = fuse->socSpeedo;
        gContext.iddq[0] = fuse->cpuIDDQ;
        gContext.iddq[1] = fuse->gpuIDDQ;
        gContext.iddq[2] = fuse->socIDDQ;
        gContext.waferX = fuse->waferX;
        gContext.waferY = fuse->waferY;

        gContext.dramID = board::GetDramID();
        gContext.isDram8GB = board::IsDram8GB();
        board::SetGpuSchedulingMode((GpuSchedulingMode)config::GetConfigValue(HorizonOCConfigValue_GPUScheduling), (GpuSchedulingOverrideMethod)config::GetConfigValue(HorizonOCConfigValue_GPUSchedulingMethod));
        gContext.gpuSchedulingMode = (GpuSchedulingMode)config::GetConfigValue(HorizonOCConfigValue_GPUScheduling);

        gContext.isSysDockInstalled = integrations::GetSysDockState();
        gContext.isSaltyNXInstalled = integrations::GetSaltyNXState();
        if (gContext.isSaltyNXInstalled) {
            integrations::LoadSaltyNX();
        }

        gContext.isUsingRetroSuper = integrations::GetRETROSuperStatus();
        governor::startThreads();
    }

    void Exit()
    {
        governor::exitThreads();
        config::Exit();
    }

    SysClkContext GetCurrentContext()
    {
        std::scoped_lock lock{gContextMutex};
        return gContext;
    }

    void SetRunning(bool running)
    {
        gRunning = running;
    }

    bool Running()
    {
        return gRunning;
    }

    void GetFreqList(SysClkModule module, std::uint32_t *list, std::uint32_t maxCount, std::uint32_t *outCount)
    {
        ASSERT_ENUM_VALID(SysClkModule, module);

        *outCount = std::min(maxCount, gFreqTable[module].count);
        memcpy(list, &gFreqTable[module].list[0], *outCount * sizeof(gFreqTable[0].list[0]));
    }

    void Tick()
    {
        fileUtils::LogLine("CPU Temp: %d", board::GetTemperatureMilli(HorizonOCThermalSensor_CPU));
        std::scoped_lock lock{gContextMutex};
        std::uint32_t mode = 0;
        Result rc = apmExtGetCurrentPerformanceConfiguration(&mode);
        ASSERT_RESULT_OK(rc, "apmExtGetCurrentPerformanceConfiguration");

        bool isBoost = apmExtIsBoostMode(mode);

        HandleSafetyFeatures();

        if (RefreshContext() || config::Refresh()) {
            HandleMiscFeatures();
            SetClocks(isBoost);
        }
    }

    void WaitForNextTick()
    {
        if (board::GetHz(SysClkModule_MEM) > 665000000)
            svcSleepThread(config::GetConfigValue(SysClkConfigValue_PollingIntervalMs) * 1000000ULL);
        else
            svcSleepThread(5000 * 1000000ULL); // 5 seconds in sleep mode
    }
} // namespace clockManager
