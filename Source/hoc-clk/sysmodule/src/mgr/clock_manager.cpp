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
#include "../file/file_utils.hpp"
#include "../board/board.hpp"
#include "../hos/process_management.hpp"
#include "../file/errors.hpp"
#include "../ipc/ipc_service.hpp"
#include "../file/kip.hpp"
#include <i2c.h>
#include "../i2c/i2cDrv.h"
#include "../display/display_refresh_rate.hpp"
#include <cstdio>
#include <crc32.h>
#include "../file/config.hpp"
#include "../hos/integrations.hpp"
#include "../util/lockable_mutex.h"
#include "../file/kip.hpp"
#include "governor.hpp"
#include "../display/aula.hpp"
#include "../soc/gm20b.hpp"

#define HOSPPC_HAS_BOOST (hosversionAtLeast(7,0,0))

namespace clockManager {


    bool gRunning = false;
    LockableMutex gContextMutex;
    HocClkContext gContext = {};
    FreqTable gFreqTable[HocClkModule_EnumMax];
    std::uint64_t gLastTempLogNs = 0;
    std::uint64_t gLastFreqLogNs = 0;
    std::uint64_t gLastPowerLogNs = 0;
    std::uint64_t gLastCsvWriteNs = 0;

    bool IsAssignableHz(HocClkModule module, std::uint32_t hz)
    {
        switch (module) {
        case HocClkModule_CPU:
            return hz >= 500000000;
        case HocClkModule_MEM:
            return hz >= 665600000;
        default:
            return true;
        }
    }

    std::uint32_t GetMaxAllowedHz(HocClkModule module, HocClkProfile profile)
    {
        if (config::GetConfigValue(HocClkConfigValue_UncappedClocks)) {
            return ~0; // Integer limit, uncapped clocks ON
        } else {
            if (module == HocClkModule_GPU) {
                if (profile < HocClkProfile_HandheldCharging) {
                    switch (board::GetSocType()) {
                    case HocClkSocType_Erista:
                        return 460800000;
                    case HocClkSocType_Mariko:
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
                } else if (profile <= HocClkProfile_HandheldChargingUSB) {
                    switch (board::GetSocType()) {
                    case HocClkSocType_Erista:
                        return 768000000;
                    case HocClkSocType_Mariko:
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
            } else if (module == HocClkModule_CPU) {
                if (profile < HocClkProfile_HandheldCharging && board::GetSocType() == HocClkSocType_Erista) {
                    return 1581000000;
                } else {
                    return ~0;
                }
            }
        }
        return 0;
    }

    std::uint32_t GetNearestHz(HocClkModule module, std::uint32_t inHz, std::uint32_t maxHz)
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
        if (config::GetConfigValue(HocClkConfigValue_LiveCpuUv)) {
            if (board::GetSocType() == HocClkSocType_Erista)
                board::SetDfllTunings(config::GetConfigValue(KipConfigValue_eristaCpuUV), 0, 1581000000);
            else
                board::SetDfllTunings(config::GetConfigValue(KipConfigValue_marikoCpuUVLow), config::GetConfigValue(KipConfigValue_marikoCpuUVHigh), board::CalculateTbreak(config::GetConfigValue(KipConfigValue_tableConf)));
        }

        board::ResetToStockGpu();
    }

    bool ConfigIntervalTimeout(HocClkConfigValue intervalMsConfigValue, std::uint64_t ns, std::uint64_t *lastLogNs)
    {
        std::uint64_t logInterval = config::GetConfigValue(intervalMsConfigValue) * 1000000ULL;
        bool shouldLog = logInterval && ((ns - *lastLogNs) > logInterval);

        if (shouldLog) {
            *lastLogNs = ns;
        }

        return shouldLog;
    }

    void RefreshFreqTableRow(HocClkModule module)
    {
        std::scoped_lock lock{gContextMutex};

        std::uint32_t freqs[HOCCLK_FREQ_LIST_MAX];
        std::uint32_t count;

        fileUtils::LogLine("[mgr] %s freq list refresh", board::GetModuleName(module, true));
        board::GetFreqList(module, &freqs[0], HOCCLK_FREQ_LIST_MAX, &count);

        std::uint32_t *hz = &gFreqTable[module].list[0];
        gFreqTable[module].count = 0;

        if (module == HocClkModule_GPU && board::GetSocType() == HocClkSocType_Mariko) {
            constexpr u32 kStep = 38400000;
            constexpr u32 kPcvStep = 76800000;
            constexpr u32 kMax = 1228800000;

            for (u32 f = kPcvStep; f <= kMax && gFreqTable[module].count < HOCCLK_FREQ_LIST_MAX; f += kStep) {
                if (f % kPcvStep != 0) {
                    if (!config::GetConfigValue(HocClkConfigValue_MarikoMiddleFreqs)) 
                        continue;
                    *hz = f;
                    gFreqTable[module].count++;
                    hz++;
                } else {
                    for (u32 i = 0; i < count; i++) {
                        if (freqs[i] == f) {
                            *hz = f;
                            gFreqTable[module].count++;
                            hz++;
                            break;
                        }
                    }
                }
            }

            for (u32 i = 0; i < count && gFreqTable[module].count < HOCCLK_FREQ_LIST_MAX; i++) {
                if (freqs[i] > kMax && IsAssignableHz(module, freqs[i])) {
                    *hz = freqs[i];
                    gFreqTable[module].count++;
                    hz++;
                }
            }
            return;
        }

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
        if (config::GetConfigValue(HocClkConfigValue_HandheldTDP) && (gContext.profile != HocClkProfile_Docked)) {
            if (board::GetConsoleType() == HocClkConsoleType_Hoag) {
                if (board::GetPowerMw(HocClkPowerSensor_Avg) < -(int)config::GetConfigValue(HocClkConfigValue_LiteTDPLimit)) {
                    ResetToStockClocks();
                    return;
                }
            } else {
                if (board::GetPowerMw(HocClkPowerSensor_Avg) < -(int)config::GetConfigValue(HocClkConfigValue_HandheldTDPLimit)) {
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
        // these dont need to run that often, so dont bother
        static u32 tick = 0;
        if(++tick > 10) {
            tick = 0;

            if (config::GetConfigValue(HocClkConfigValue_BatteryChargeCurrent)) {
                I2c_Bq24193_SetFastChargeCurrentLimit(config::GetConfigValue(HocClkConfigValue_BatteryChargeCurrent));
            }

            I2c_BuckConverter_SetMvOut(&I2c_Display, config::GetConfigValue(HocClkConfigValue_DisplayVoltage));

            if(board::GetConsoleType() == HocClkConsoleType_Aula)
                AulaDisplay::SetDisplayColorMode((AulaColorMode)config::GetConfigValue(HocClkConfigValue_AulaDisplayColorPreset));
        }
    }

    void ApplyGpuDvfs(u32 targetHz) {
        s32 dvfsOffset = config::GetConfigValue(HocClkConfigValue_DVFSOffset);
        dvfsOffset = std::max(dvfsOffset, -80);
        u32 vmin = board::GetMinimumGpuVmin(targetHz / 1000000, board::GetGpuSpeedoBracket());

        if (vmin) {
            vmin += dvfsOffset;
        }

        /* Prevent console from combusting if for some reason bad shit happens :P */
        vmin = std::min(vmin, 1000u);

        /* Get nearest gpu clock; we need this in a second to update the voltage. */
        u32 gpuHz = board::GetHz(HocClkModule_GPU);
        u32 maxHz = GetMaxAllowedHz(HocClkModule_GPU, gContext.profile);
        u32 nearestGpuHz = GetNearestHz(HocClkModule_GPU, gpuHz, maxHz);

        /* Hijack gpu volt table. */
        board::PcvHijackGpuVolts(vmin);

        /* Update gpu frequency to actually use the voltage. */
        if (targetHz) {
            board::SetHz(HocClkModule_GPU, nearestGpuHz);
        } else {
            /* If the target frequency is zero, we reset the frequency to ensure it gets updated even without any frequency override. */
            board::ResetToStockGpu();
        }
    }

    void HandleCpuUv()
    {
        if (board::GetSocType() == HocClkSocType_Erista)
            board::SetDfllTunings(config::GetConfigValue(KipConfigValue_eristaCpuUV), 0, 1581000000); // Erista tbreak is always 1581MHz
        else
            board::SetDfllTunings(config::GetConfigValue(KipConfigValue_marikoCpuUVLow), config::GetConfigValue(KipConfigValue_marikoCpuUVHigh), board::CalculateTbreak(config::GetConfigValue(KipConfigValue_tableConf)));
    }

    void DVFSReset()
    {
        if (board::GetSocType() == HocClkSocType_Mariko && config::GetConfigValue(HocClkConfigValue_DVFSMode) == DVFSMode_Hijack) {
            board::PcvHijackGpuVolts(0); // Reset to vMin

            u32 targetHz = gContext.overrideFreqs[HocClkModule_GPU];
            if (!targetHz) {
                targetHz = config::GetAutoClockHz(gContext.applicationId, HocClkModule_GPU, gContext.profile, false);
                if (!targetHz) {
                    targetHz = config::GetAutoClockHz(HOCCLK_GLOBAL_PROFILE_TID, HocClkModule_GPU, gContext.profile, false);
                }
            }
            u32 maxHz = GetMaxAllowedHz(HocClkModule_GPU, gContext.profile);
            u32 nearestHz = GetNearestHz(HocClkModule_GPU, targetHz, maxHz);

            board::ResetToStockGpu();
            if (targetHz)
                board::SetHz(HocClkModule_GPU, nearestHz);
        }
    }

    void HandleFreqReset(HocClkModule module, bool isBoost, bool didHijackPcv)
    {
        switch (module) {
            case HocClkModule_CPU:
                if (!(isBoost || (config::GetConfigValue(HocClkConfigValue_OverwriteBoostMode) && isBoost)))
                    board::ResetToStockCpu();
                if (config::GetConfigValue(HocClkConfigValue_LiveCpuUv)) {
                    if (board::GetSocType() == HocClkSocType_Erista)
                        board::SetDfllTunings(config::GetConfigValue(KipConfigValue_eristaCpuUV), 0, 1581000000);
                    else
                        board::SetDfllTunings(config::GetConfigValue(KipConfigValue_marikoCpuUVLow), config::GetConfigValue(KipConfigValue_marikoCpuUVHigh), board::CalculateTbreak(config::GetConfigValue(KipConfigValue_tableConf)));
                }
                break;
            case HocClkModule_GPU:
                board::ResetToStockGpu();
                break;
            case HocClkModule_MEM:
                board::ResetToStockMem();
                if(!didHijackPcv) {
                    DVFSReset();
                    didHijackPcv = true;
                }
                break;
            case HocClkModule_Display:
                if (config::GetConfigValue(HocClkConfigValue_OverwriteRefreshRate)) {
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
        static bool prepareBoostExit = false;

        bool didHijackPcv = false;
        bool skipCpuDueToBoost = isBoost && !config::GetConfigValue(HocClkConfigValue_OverwriteBoostMode);
        if (skipCpuDueToBoost) {
            board::SetHz(HocClkModule_CPU, board::GetHz(HocClkModule_CPU));
            prepareBoostExit = true;
            return; // Return if we aren't overwriting boost mode
        }

        if (prepareBoostExit) {
            board::SetHz(HocClkModule_CPU, board::GetHz(HocClkModule_CPU));
            prepareBoostExit = false;
        }
        bool returnRaw = false; // Return a value scaled to MHz instead of raw value
        for (unsigned int module = 0; module < HocClkModule_EnumMax; module++) {
            u32 oldHz = board::GetHz((HocClkModule)module); // Get Old hz (used primarily for DVFS Logic)

            if (module > HocClkModule_MEM)
                returnRaw = true;
            else
                returnRaw = false;
            targetHz = gContext.overrideFreqs[module];
            if (!targetHz) {
                targetHz = config::GetAutoClockHz(gContext.applicationId, (HocClkModule)module, gContext.profile, returnRaw);
                if (!targetHz)
                    targetHz = config::GetAutoClockHz(HOCCLK_GLOBAL_PROFILE_TID, (HocClkModule)module, gContext.profile, returnRaw);
            }

            if (module == HocClkModule_Governor) {
                governor::HandleGovernor(targetHz);
            }

            bool noCPU = governor::isCpuGovernorEnabled;
            bool noGPU = governor::isGpuGovernorEnabled;
            bool noDisp = governor::isVRREnabled;
            if (noDisp && module == HocClkModule_Display)
                continue;

            if (module == HocClkModule_Display && config::GetConfigValue(HocClkConfigValue_OverwriteRefreshRate) && !noDisp) {
                if (targetHz) {
                    board::SetHz(HocClkModule_Display, targetHz);
                    gContext.freqs[HocClkModule_Display] = targetHz;
                    gContext.realFreqs[HocClkModule_Display] = targetHz;

                    gContext.stable.freqs[HocClkModule_Display] = targetHz;
                    gContext.stable.realFreqs[HocClkModule_Display] = targetHz;
                } else {
                    HandleFreqReset(HocClkModule_Display, isBoost, didHijackPcv);
                }
            }

            // The modules above MEM require special handling
            if (module > HocClkModule_MEM) {
                continue;
            }

            if ((skipCpuDueToBoost || noCPU) && module == HocClkModule_CPU)
                continue;
            if (noGPU && module == HocClkModule_GPU)
                continue;

            if (targetHz) {
                maxHz = GetMaxAllowedHz((HocClkModule)module, gContext.profile);
                nearestHz = GetNearestHz((HocClkModule)module, targetHz, maxHz);

                if (nearestHz != gContext.freqs[module]) {
                    fileUtils::LogLine(
                        "[mgr] %s clock set : %u.%u MHz (target = %u.%u MHz)",
                        board::GetModuleName((HocClkModule)module, true),
                        nearestHz / 1000000, nearestHz / 100000 - nearestHz / 1000000 * 10,
                        targetHz / 1000000, targetHz / 100000 - targetHz / 1000000 * 10
                    );

                    // The logic MUST be done in this order otherwise you WILL get crashes
                    if (module == HocClkModule_MEM && board::GetSocType() == HocClkSocType_Mariko && targetHz > oldHz && config::GetConfigValue(HocClkConfigValue_DVFSMode) == DVFSMode_Hijack) {
                        ApplyGpuDvfs(targetHz);
                    }

                    board::SetHz((HocClkModule)module, nearestHz);
                    gContext.freqs[module] = nearestHz;

                    if (module < HocClkModuleStable_EnumMax) {
                        gContext.stable.freqs[module] = nearestHz;
                    }

                    if (module == HocClkModule_CPU && config::GetConfigValue(HocClkConfigValue_LiveCpuUv)) {
                        HandleCpuUv();
                    }

                    if (module == HocClkModule_MEM && board::GetSocType() == HocClkSocType_Mariko && targetHz < oldHz && config::GetConfigValue(HocClkConfigValue_DVFSMode) == DVFSMode_Hijack) {
                        ApplyGpuDvfs(targetHz);
                    }

                    if(module == HocClkModule_MEM && board::GetSocType() == HocClkSocType_Mariko && config::GetConfigValue(HocClkConfigValue_DVFSMode) == DVFSMode_Hijack)
                        didHijackPcv = false;
                }
            } else {
                HandleFreqReset((HocClkModule)module, isBoost, didHijackPcv);
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

        HocClkProfile profile = board::GetProfile();
        if (profile != gContext.profile) {
            fileUtils::LogLine("[mgr] Profile change: %s", board::GetProfileName(profile, true));
            gContext.profile = profile;
            hasChanged = true;
        }

        // restore clocks to stock values on app or profile change
        if (hasChanged) {
            board::ResetToStock();
            if (board::GetSocType() == HocClkSocType_Mariko && config::GetConfigValue(HocClkConfigValue_DVFSMode) == DVFSMode_Hijack) {
                board::PcvHijackGpuVolts(0);
                board::ResetToStockGpu();
            }
            WaitForNextTick();
        }

        std::uint32_t hz = 0;
        for (unsigned int module = 0; module < HocClkModule_EnumMax; module++) {
            hz = board::GetHz((HocClkModule)module);
            if (hz != 0 && hz != gContext.freqs[module]) {
                fileUtils::LogLine("[mgr] %s clock change: %u.%u MHz", board::GetModuleName((HocClkModule)module, true), hz / 1000000, hz / 100000 - hz / 1000000 * 10);
                gContext.freqs[module] = hz;

                if (module < HocClkModuleStable_EnumMax) {
                    gContext.stable.freqs[module] = hz;
                }
                hasChanged = true;
            }

            hz = config::GetOverrideHz((HocClkModule)module);
            if (hz != gContext.overrideFreqs[module]) {
                if (hz) {
                    fileUtils::LogLine("[mgr] %s override change: %u.%u MHz", board::GetModuleName((HocClkModule)module, true), hz / 1000000, hz / 100000 - hz / 1000000 * 10);
                }
                gContext.overrideFreqs[module] = hz;

                if (module < HocClkModuleStable_EnumMax) {
                    gContext.stable.overrideFreqs[module] = hz;
                }
                hasChanged = true;
            }
        }

        std::uint64_t ns = armTicksToNs(armGetSystemTick());

        // temperatures do not and should not force a refresh, hasChanged untouched
        std::uint32_t millis = 0;
        bool shouldLogTemp = ConfigIntervalTimeout(HocClkConfigValue_TempLogIntervalMs, ns, &gLastTempLogNs);
        for (unsigned int sensor = 0; sensor < HocClkThermalSensor_EnumMax; sensor++) {
            millis = board::GetTemperatureMilli((HocClkThermalSensor)sensor);
            if (shouldLogTemp) {
                fileUtils::LogLine("[mgr] %s temp: %u.%u °C", board::GetThermalSensorName((HocClkThermalSensor)sensor, true), millis / 1000, (millis - millis / 1000 * 1000) / 100);
            }
            gContext.temps[sensor] = millis;

            if (sensor < HocClkThermalSensorStable_EnumMax) {
                gContext.stable.temps[sensor] = millis;
            }
        }

        // power stats do not and should not force a refresh, hasChanged untouched
        std::int32_t mw = 0;
        bool shouldLogPower = ConfigIntervalTimeout(HocClkConfigValue_PowerLogIntervalMs, ns, &gLastPowerLogNs);
        for (unsigned int sensor = 0; sensor < HocClkPowerSensor_EnumMax; sensor++) {
            mw = board::GetPowerMw((HocClkPowerSensor)sensor);
            if (shouldLogPower) {
                fileUtils::LogLine("[mgr] Power %s: %d mW", board::GetPowerSensorName((HocClkPowerSensor)sensor, false), mw);
            }
            gContext.power[sensor] = mw;

            if (sensor < HocClkPowerSensorStable_EnumMax) {
                gContext.stable.power[sensor] = mw;
            }
        }

        // real freqs do not and should not force a refresh, hasChanged untouched
        std::uint32_t realHz = 0;
        bool shouldLogFreq = ConfigIntervalTimeout(HocClkConfigValue_FreqLogIntervalMs, ns, &gLastFreqLogNs);
        for (unsigned int module = 0; module < HocClkModule_EnumMax; module++) {
            realHz = board::GetRealHz((HocClkModule)module);
            if (shouldLogFreq) {
                fileUtils::LogLine("[mgr] %s real freq: %u.%u MHz", board::GetModuleName((HocClkModule)module, true), realHz / 1000000, realHz / 100000 - realHz / 1000000 * 10);
            }
            gContext.realFreqs[module] = realHz;

            if (module < HocClkModuleStable_EnumMax) {
                gContext.stable.realFreqs[module] = realHz;
            }
        }

        // ram load do not and should not force a refresh, hasChanged untouched
        for (unsigned int loadSource = 0; loadSource < HocClkPartLoad_EnumMax; loadSource++) {
            gContext.partLoad[loadSource] = board::GetPartLoad((HocClkPartLoad)loadSource);

            if (loadSource < HocClkPartLoadStable_EnumMax) {
                gContext.stable.partLoad[loadSource] = board::GetPartLoad((HocClkPartLoad)loadSource);
            }
        }

        for (unsigned int voltageSource = 0; voltageSource < HocClkVoltage_EnumMax; voltageSource++) {
            gContext.voltages[voltageSource] = board::GetVoltage((HocClkVoltage)voltageSource);

            if (voltageSource < HocClkVoltageStable_EnumMax) {
                gContext.stable.voltages[voltageSource] = board::GetVoltage((HocClkVoltage)voltageSource);
            }
        }

        if (ConfigIntervalTimeout(HocClkConfigValue_CsvWriteIntervalMs, ns, &gLastCsvWriteNs)) {
            fileUtils::WriteContextToCsv(&gContext);
        }

        // this->context->maxDisplayFreq = board::GetHighestDockedDisplayRate();
        u32 targetHz = gContext.overrideFreqs[HocClkModule_Display];
        if (!targetHz) {
            targetHz = config::GetAutoClockHz(gContext.applicationId, HocClkModule_Display, gContext.profile, true);
            if (!targetHz)
                targetHz = config::GetAutoClockHz(HOCCLK_GLOBAL_PROFILE_TID, HocClkModule_Display, gContext.profile, true);
        }

        if (board::GetConsoleType() != HocClkConsoleType_Hoag)
            board::SetDisplayRefreshDockedState(gContext.profile == HocClkProfile_Docked);

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
        gContext = {};
        gContext.applicationId = 0;
        gContext.profile = HocClkProfile_Handheld;
        for (unsigned int module = 0; module < HocClkModule_EnumMax; module++) {
            gContext.freqs[module] = 0;
            gContext.realFreqs[module] = 0;
            gContext.overrideFreqs[module] = 0;

            if (module < HocClkModuleStable_EnumMax) {
                gContext.stable.freqs[module] = 0;
                gContext.stable.realFreqs[module] = 0;
                gContext.stable.overrideFreqs[module] = 0;
            }

            RefreshFreqTableRow((HocClkModule)module);
        }

        gRunning = false;
        gLastTempLogNs = 0;
        gLastCsvWriteNs = 0;

        kip::GetKipData();

        board::FuseData *fuse = board::GetFuseData();

        gContext.speedos[HocClkSpeedo_CPU] = fuse->cpuSpeedo;
        gContext.speedos[HocClkSpeedo_GPU] = fuse->gpuSpeedo;
        gContext.speedos[HocClkSpeedo_SOC] = fuse->socSpeedo;
        gContext.iddq[HocClkSpeedo_CPU] = fuse->cpuIDDQ;
        gContext.iddq[HocClkSpeedo_GPU] = fuse->gpuIDDQ;
        gContext.iddq[HocClkSpeedo_SOC] = fuse->socIDDQ;
        gContext.waferX = fuse->waferX;
        gContext.waferY = fuse->waferY;

        gContext.dramID = board::GetDramID();
        gContext.isDram8GB = board::IsDram8GB();
        gContext.consoleType = board::GetConsoleType();
        
        board::SetGpuSchedulingMode((GpuSchedulingMode)config::GetConfigValue(HocClkConfigValue_GPUScheduling), (GpuSchedulingOverrideMethod)config::GetConfigValue(HocClkConfigValue_GPUSchedulingMethod));
        gContext.gpuSchedulingMode = (GpuSchedulingMode)config::GetConfigValue(HocClkConfigValue_GPUScheduling);

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
    }

    HocClkContext GetCurrentContext()
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

    void GetFreqList(HocClkModule module, std::uint32_t *list, std::uint32_t maxCount, std::uint32_t *outCount)
    {
        ASSERT_ENUM_VALID(HocClkModule, module);

        *outCount = std::min(maxCount, gFreqTable[module].count);
        memcpy(list, &gFreqTable[module].list[0], *outCount * sizeof(gFreqTable[0].list[0]));
    }

    void Tick()
    {
        std::scoped_lock lock{gContextMutex};
        std::uint32_t mode = 0;
        Result rc = apmExtGetCurrentPerformanceConfiguration(&mode);
        ASSERT_RESULT_OK(rc, "apmExtGetCurrentPerformanceConfiguration");

        bool isBoost = apmExtIsBoostMode(mode);

        HandleSafetyFeatures();
        HandleMiscFeatures();
        
        // GPU clock should always be the same unless PCV has overwriten our change, so reset it
        if (RefreshContext() || config::Refresh() || board::GetRealHz(HocClkModule_GPU) != gContext.freqs[HocClkModule_GPU]) {
            SetClocks(isBoost);
        }
    }

    void WaitForNextTick()
    {
        if (board::GetHz(HocClkModule_MEM) > 665000000)
            svcSleepThread(config::GetConfigValue(HocClkConfigValue_PollingIntervalMs) * 1000000ULL);
        else
            svcSleepThread(5000 * 1000000ULL); // 5 seconds in sleep mode
    }
} // namespace clockManager
