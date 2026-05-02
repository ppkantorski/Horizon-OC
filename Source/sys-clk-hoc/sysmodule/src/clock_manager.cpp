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
#include <i2c.h>
#include "board/display_refresh_rate.hpp"
#include <cstdio>
#include <crc32.h>
#include "config.hpp"
#include "integrations.hpp"
#include <nxExt/cpp/lockable_mutex.h>
#include "governor.hpp"
#include "kip.hpp"

#define HOSPPC_HAS_BOOST (hosversionAtLeast(7,0,0))

namespace clockManager {


    bool gRunning = false;
    bool gPrevEnabled = true; // matches main()'s initial config::SetEnabled(true)
    bool gPrevIsBoost = false;
    s32  s_lastDvfsOffset = INT32_MIN; // sentinel: "never applied"
    LockableMutex gContextMutex;                                             // guards gContext (tick + governor threads)
    LockableMutex gSnapshotMutex;                                            // guards gContextSnapshot (tick + IPC thread only)
    HocClkContext gContext = {};
    HocClkContext gContextSnapshot = {};  // IPC-visible snapshot; updated at end of each Tick()
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
        static u32 tick = 0;
        if(++tick > 10) {
            tick = 0;
            if (config::GetConfigValue(HocClkConfigValue_BatteryChargeCurrent)) {
                I2c_Bq24193_SetFastChargeCurrentLimit(config::GetConfigValue(HocClkConfigValue_BatteryChargeCurrent));
            }
            I2c_BuckConverter_SetMvOut(&I2c_Display, config::GetConfigValue(HocClkConfigValue_DisplayVoltage));
        }
    }

    void DVFSBeforeSet(u32 memTargetHz)
    {
        s32 dvfsOffset = config::GetConfigValue(HocClkConfigValue_DVFSOffset);
        // Raw DVFS floor: the minimum GPU voltage the memory controller needs at
        // this MEM speed.  The offset adjusts this floor up or down — it does NOT
        // shift every table entry.  PcvHijackGpuVolts only raises entries that are
        // naturally BELOW the adjusted floor; GPU frequency entries already above the
        // floor are never touched.  This is intentional: the slider is for fine-tuning
        // the RAM OC stability margin, not for general GPU UV.
        u32 rawFloor = board::GetMinimumGpuVmin(memTargetHz / 1000000, board::GetGpuSpeedoBracket());
        s32 adjusted = (s32)rawFloor + dvfsOffset;
        u32 vmin = (u32)std::max(0, std::min(1000, adjusted));

        fileUtils::LogLine("[dvfs] DVFSBeforeSet: memHz=%u, rawFloor=%u, offset=%d, vmin=%u",
            memTargetHz, rawFloor, dvfsOffset, vmin);

        board::PcvHijackGpuVolts(vmin);

        // Raise hardware voltage immediately if below the required floor.
        // This covers the gap between the table write and the next PCV GPU event.
        // Only raises (never lowers), so safe to call unconditionally.
        if (vmin && I2c_BuckConverter_GetMvOut(&I2c_Mariko_GPU) < vmin) {
            I2c_BuckConverter_SetMvOut(&I2c_Mariko_GPU, vmin);
        }

        gContext.voltages[HocClkVoltage_GPU] = vmin * 1000;
    }

    void DVFSAfterSet(u32 memTargetHz)
    {
        s32 dvfsOffset = config::GetConfigValue(HocClkConfigValue_DVFSOffset);
        u32 rawFloor = board::GetMinimumGpuVmin(memTargetHz / 1000000, board::GetGpuSpeedoBracket());
        s32 adjusted = (s32)rawFloor + dvfsOffset;
        u32 vmin = (u32)std::max(0, std::min(1000, adjusted));

        fileUtils::LogLine("[dvfs] DVFSAfterSet: memHz=%u, rawFloor=%u, offset=%d, vmin=%u",
            memTargetHz, rawFloor, dvfsOffset, vmin);

        // PcvHijackGpuVolts restores the original table when vmin=0 (MEM downscale
        // below DVFS threshold), and raises entries below the floor for higher speeds.
        // Entries naturally above the floor are never modified.
        board::PcvHijackGpuVolts(vmin);

        // PCV only re-evaluates its voltage table when a GPU clock-rate change arrives.
        // Bounce the GPU clock so PCV traverses the freshly-written table immediately.
        // Use board::GetHz (live hardware read) so the bounce fires even when no GPU
        // override is active (gContext.freqs[GPU] is 0 in that case).
        u32 gpuHz = board::GetHz(HocClkModule_GPU);
        if (gpuHz) {
            board::SetHz(HocClkModule_GPU, ~0u);
            board::SetHz(HocClkModule_GPU, gpuHz);
        }

        fileUtils::LogLine("[dvfs] DVFSAfterSet: done");
    }

    // Called after a GPU clock change to keep the voltage table in sync.
    // Uses the CURRENT MEM frequency to compute the floor — NOT the GPU frequency.
    void DVFSVoltUpdate()
    {
        s32 dvfsOffset = config::GetConfigValue(HocClkConfigValue_DVFSOffset);
        u32 memHz = gContext.freqs[HocClkModule_MEM];
        u32 rawFloor = board::GetMinimumGpuVmin(memHz / 1000000, board::GetGpuSpeedoBracket());
        s32 adjusted = (s32)rawFloor + dvfsOffset;
        u32 vmin = (u32)std::max(0, std::min(1000, adjusted));

        fileUtils::LogLine("[dvfs] DVFSVoltUpdate: memHz=%u, rawFloor=%u, offset=%d, vmin=%u",
            memHz, rawFloor, dvfsOffset, vmin);

        board::PcvHijackGpuVolts(vmin);

        // The SetHz(GPU) that triggered this function already ran with the old table.
        // Bounce to force PCV to traverse the freshly-written table now.
        u32 gpuHz = board::GetHz(HocClkModule_GPU);
        if (gpuHz) {
            board::SetHz(HocClkModule_GPU, ~0u);
            board::SetHz(HocClkModule_GPU, gpuHz);
        }
    }

    void HandleCpuUv()
    {
        if (board::GetSocType() == HocClkSocType_Erista)
            board::SetDfllTunings(config::GetConfigValue(KipConfigValue_eristaCpuUV), 0, 1581000000);
        else
            board::SetDfllTunings(config::GetConfigValue(KipConfigValue_marikoCpuUVLow), config::GetConfigValue(KipConfigValue_marikoCpuUVHigh), board::CalculateTbreak(config::GetConfigValue(KipConfigValue_tableConf)));
    }

    void DVFSReset()
    {
        if (board::GetSocType() == HocClkSocType_Mariko && config::GetConfigValue(HocClkConfigValue_DVFSMode) == DVFSMode_Hijack) {
            fileUtils::LogLine("[dvfs] DVFSReset: un-hijacking");
            board::PcvHijackGpuVolts(0);

            u32 targetHz = gContext.overrideFreqs[HocClkModule_GPU];
            if (!targetHz) {
                targetHz = config::GetAutoClockHz(gContext.applicationId, HocClkModule_GPU, gContext.profile, false);
                if (!targetHz) {
                    targetHz = config::GetAutoClockHz(HOCCLK_GLOBAL_PROFILE_TID, HocClkModule_GPU, gContext.profile, false);
                }
            }
            u32 maxHz = GetMaxAllowedHz(HocClkModule_GPU, gContext.profile);
            u32 nearestHz = GetNearestHz(HocClkModule_GPU, targetHz, maxHz);

            fileUtils::LogLine("[dvfs] DVFSReset: targetHz=%u nearestHz=%u", targetHz, nearestHz);
            if (targetHz) {
                board::SetHz(HocClkModule_GPU, ~0u);
                board::SetHz(HocClkModule_GPU, nearestHz);
            } else {
                board::SetHz(HocClkModule_GPU, ~0u);
                board::ResetToStockGpu();
            }
            s_lastDvfsOffset = INT32_MIN; // force re-apply when DVFS re-enables
            fileUtils::LogLine("[dvfs] DVFSReset: done");
        }
    }

    void HandleFreqReset(HocClkModule module, bool isBoost)
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
            DVFSReset();
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

        // BUG FIX: Clock drops (CPU/GPU fall to stock randomly in games).
        //
        // Root cause: the early "return" here exited SetClocks() entirely
        // whenever the system was in boost mode and OverwriteBoostMode was
        // off, leaving GPU and MEM completely unmanaged.
        //
        // The problematic sequence:
        //   1. App/profile change → RefreshContext() calls ResetToStock() +
        //      WaitForNextTick(), then re-reads hardware (now at stock) into
        //      gContext.freqs.  Returns true.
        //   2. SetClocks() called with isBoost=true (common at game launch).
        //   3. Old code returned early → GPU/MEM left at stock.
        //   4. gContext.freqs[GPU/MEM] == board::GetHz(GPU/MEM) == stock.
        //   5. Next tick: RefreshContext() detects no change → SetClocks()
        //      never called again → GPU/MEM permanently stuck at stock
        //      until some other incidental change triggered a new call.
        //
        // Fix: replace the early return with a flag that skips only CPU
        // (which is the only thing boost mode should control).  GPU and
        // MEM continue to be applied normally.
        bool skipCpuDueToBoost = isBoost && !config::GetConfigValue(HocClkConfigValue_OverwriteBoostMode);
        if (skipCpuDueToBoost) {
            u32 boostFreq = board::GetHz(HocClkModule_CPU);
            if (boostFreq / 1000000 > 1785) {
                board::SetHz(HocClkModule_CPU, boostFreq);
            }
            // Intentionally NOT returning here — GPU and MEM still need to be managed.
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
                } else {
                    HandleFreqReset(HocClkModule_Display, isBoost);
                }
            }

            // Skip GPU and CPU if governors handle them
            if (module > HocClkModule_MEM) {
                continue;
            }

            // Skip CPU when deferred to boost mode OR when the CPU governor is active.
            // Exception: when OverwriteBoostMode is set AND we're in boost, the governor
            // yields to us (it now sleeps instead of looping), so the tick thread must
            // apply the configured CPU target here.  Without this carve-out the governor's
            // isCpuGovernorEnabled=true would make noCPU=true and skip CPU, leaving
            // nobody to apply the user's boost-clock override.
            bool governorOwnerCpu = noCPU && !(isBoost && (bool)config::GetConfigValue(HocClkConfigValue_OverwriteBoostMode));
            if ((skipCpuDueToBoost || governorOwnerCpu) && module == HocClkModule_CPU)
                continue;
            if (noGPU && module == HocClkModule_GPU)
                continue;

            // Re-apply the DVFS floor when the GPU vmin offset slider changes.
            // The offset adjusts rawFloor+dvfsOffset — only entries below that adjusted
            // floor are raised; entries naturally above the floor are untouched.
            // Bounce the GPU clock after so PCV re-reads the newly-written table.
            if (module == HocClkModule_GPU && board::GetSocType() == HocClkSocType_Mariko
                && config::GetConfigValue(HocClkConfigValue_DVFSMode) == DVFSMode_Hijack) {
                s32 currentOffset = (s32)config::GetConfigValue(HocClkConfigValue_DVFSOffset);
                if (currentOffset != s_lastDvfsOffset) {
                    u32 memHz = board::GetHz(HocClkModule_MEM); // live read, not stale cache
                    u32 rawFloor = board::GetMinimumGpuVmin(memHz / 1000000, board::GetGpuSpeedoBracket());
                    s32 adj = (s32)rawFloor + currentOffset;
                    u32 vmin = (u32)std::max(0, std::min(1000, adj));
                    fileUtils::LogLine("[dvfs] offset changed %d -> %d, memHz=%u, rawFloor=%u, vmin=%u",
                        s_lastDvfsOffset, currentOffset, memHz, rawFloor, vmin);
                    board::PcvHijackGpuVolts(vmin);
                    s_lastDvfsOffset = currentOffset;
                    // Bounce GPU so PCV traverses the freshly-written table.
                    // MUST use board::GetHz (live hardware read) — gContext.freqs[GPU]
                    // is 0 when no GPU override is active, which would silently skip
                    // the bounce.
                    u32 gpuHz = board::GetHz(HocClkModule_GPU);
                    if (gpuHz) {
                        board::SetHz(HocClkModule_GPU, ~0u);
                        board::SetHz(HocClkModule_GPU, gpuHz);
                    }
                }
            }

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

                    if (module == HocClkModule_MEM && board::GetSocType() == HocClkSocType_Mariko
                        && targetHz > oldHz
                        && config::GetConfigValue(HocClkConfigValue_DVFSMode) == DVFSMode_Hijack) {
                        DVFSBeforeSet(targetHz);
                    }

                    if (module == HocClkModule_MEM) {
                        fileUtils::LogLine("[mgr] SetHz(MEM, %u) calling...", nearestHz);
                    }
                    board::SetHz((HocClkModule)module, nearestHz);
                    if (module == HocClkModule_MEM) {
                        fileUtils::LogLine("[mgr] SetHz(MEM, %u) returned", nearestHz);
                    }
                    gContext.freqs[module] = nearestHz;

                    if (module == HocClkModule_CPU && config::GetConfigValue(HocClkConfigValue_LiveCpuUv)) {
                        HandleCpuUv();
                    }

                    if (module == HocClkModule_MEM && board::GetSocType() == HocClkSocType_Mariko
                        && targetHz < oldHz
                        && config::GetConfigValue(HocClkConfigValue_DVFSMode) == DVFSMode_Hijack) {
                        DVFSAfterSet(targetHz);
                    }

                    // GPU going DOWN: lower voltage after lowering the clock.
                    // DVFSVoltUpdate (not DVFSAfterSet) because the SetHz already happened.
                    if (module == HocClkModule_GPU && board::GetSocType() == HocClkSocType_Mariko
                        && targetHz < oldHz
                        && config::GetConfigValue(HocClkConfigValue_DVFSMode) == DVFSMode_Hijack) {
                        DVFSVoltUpdate();
                    }

                }
            } else {
                HandleFreqReset((HocClkModule)module, isBoost);
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
            fileUtils::LogLine("[mgr] hasChanged: ResetToStock + DVFSReset starting");
            board::ResetToStock();
            if (board::GetSocType() == HocClkSocType_Mariko && config::GetConfigValue(HocClkConfigValue_DVFSMode) == DVFSMode_Hijack) {
                fileUtils::LogLine("[mgr] hasChanged: PcvHijackGpuVolts(0) + GPU reset");
                board::PcvHijackGpuVolts(0);
                board::ResetToStockGpu();
                // Hardware settle time: PCV needs a full tick interval after the
                // DVFS hijack is torn down and GPU state is reset, otherwise an
                // EMC clock change at high frequencies can race against PCV's
                // voltage management.
                //
                // This sleep belongs INSIDE the DVFS-hijack block.  It was
                // previously placed after the closing brace, making it run on
                // every app/profile change on every device — causing an
                // unnecessary 300 ms stall at every game launch on Erista and
                // on Mariko with DVFSMode != Hijack.  During that stall boost
                // mode could start, meaning the tick woke up with a stale
                // isBoost=false and applied the plain profile clock for one
                // extra tick before eventually landing on the boost target.
                WaitForNextTick();
            }
            fileUtils::LogLine("[mgr] hasChanged: done");
        }

        std::uint32_t hz = 0;
        for (unsigned int module = 0; module < HocClkModule_EnumMax; module++) {
            hz = board::GetHz((HocClkModule)module);
            if (hz != 0 && hz != gContext.freqs[module]) {
                fileUtils::LogLine("[mgr] %s clock change: %u.%u MHz", board::GetModuleName((HocClkModule)module, true), hz / 1000000, hz / 100000 - hz / 1000000 * 10);
                gContext.freqs[module] = hz;
                hasChanged = true;
            }

            hz = config::GetOverrideHz((HocClkModule)module);
            if (hz != gContext.overrideFreqs[module]) {
                if (hz) {
                    fileUtils::LogLine("[mgr] %s override change: %u.%u MHz", board::GetModuleName((HocClkModule)module, true), hz / 1000000, hz / 100000 - hz / 1000000 * 10);
                }
                gContext.overrideFreqs[module] = hz;
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
        }

        // ram load do not and should not force a refresh, hasChanged untouched
        for (unsigned int loadSource = 0; loadSource < HocClkPartLoad_EnumMax; loadSource++) {
            gContext.partLoad[loadSource] = board::GetPartLoad((HocClkPartLoad)loadSource);
        }

        for (unsigned int voltageSource = 0; voltageSource < HocClkVoltage_EnumMax; voltageSource++) {
            gContext.voltages[voltageSource] = board::GetVoltage((HocClkVoltage)voltageSource);
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
            RefreshFreqTableRow((HocClkModule)module);
        }

        gRunning = false;
        gLastTempLogNs = 0;
        gLastCsvWriteNs = 0;

        // Read the externally-managed KIP at sdmc:/atmosphere/kips/hoc.kip
        // and populate KipConfigValue_* into the in-memory config. This is
        // READ-ONLY (kip.cpp never writes the KIP) - the third-party tool
        // owns the KIP file. Without this call, KipConfigValue_marikoGpuUV
        // and several CPU undervolt values stay at zero, which silently
        // caps the GPU at 614.4 MHz and (when LiveCpuUv is on) zeroes the
        // DFLL tunings. Both can destabilise the SoC at high EMC frequencies.
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
        board::SetGpuSchedulingMode((GpuSchedulingMode)config::GetConfigValue(HocClkConfigValue_GPUScheduling), (GpuSchedulingOverrideMethod)config::GetConfigValue(HocClkConfigValue_GPUSchedulingMethod));
        gContext.gpuSchedulingMode = (GpuSchedulingMode)config::GetConfigValue(HocClkConfigValue_GPUScheduling);

        gContext.isSysDockInstalled = integrations::GetSysDockState();
        gContext.isSaltyNXInstalled = integrations::GetSaltyNXState();
        if (gContext.isSaltyNXInstalled) {
            integrations::LoadSaltyNX();
        }

        gContext.isUsingRetroSuper = integrations::GetRETROSuperStatus();
        governor::startThreads();

        // Seed IPC snapshot so the first GetCurrentContext() call returns valid data
        // even before the first Tick() completes.
        {
            std::scoped_lock lock{gSnapshotMutex};
            gContextSnapshot = gContext;
        }
    }

    void Exit()
    {
        governor::exitThreads();
    }

    HocClkContext GetCurrentContext()
    {
        std::scoped_lock lock{gSnapshotMutex};
        return gContextSnapshot;
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
        // Hold gContextMutex to guard gContext against concurrent governor-thread writes.
        // Governor threads take this same lock before modifying gContext.freqs[CPU/GPU/Display].
        // GetCurrentContext() (IPC) reads gContextSnapshot under gSnapshotMutex — a completely
        // separate lock — so it is never blocked by holding gContextMutex here.
        std::scoped_lock lock{gContextMutex};

        std::uint32_t mode = 0;
        Result rc = apmExtGetCurrentPerformanceConfiguration(&mode);
        ASSERT_RESULT_OK(rc, "apmExtGetCurrentPerformanceConfiguration");

        bool isBoost = apmExtIsBoostMode(mode);

        HandleSafetyFeatures();
        HandleMiscFeatures();

        // ---------------------------------------------------------------
        // BUG FIX: Enable button was not gating clock management.
        //
        // config::Enabled() is set by the overlay's Enable toggle via the
        // IPC SetEnabled command, but Tick() never checked it — SetClocks()
        // was called whenever any context/config change was detected,
        // regardless of the enabled state.  The overlay toggle appeared to
        // flip visually but had no effect on actual clock management.
        //
        // Fix: track the previous enabled state here.  On any transition
        // (enabled→disabled or disabled→enabled) OR on any normal context/
        // config change, take the appropriate action:
        //   • disabled: reset all clocks to stock so we leave the hardware
        //     in a clean state (also prevents stale OC clocks after an app
        //     switch while the module is disabled).
        //   • enabled:  apply configured clocks immediately on re-enable,
        //     not only on the next incidental context change.
        // ---------------------------------------------------------------
        bool enabled = config::Enabled();
        bool enabledChanged = (enabled != gPrevEnabled);
        if (enabledChanged) {
            fileUtils::LogLine("[mgr] Enabled changed: %s", enabled ? "on" : "off");
            gPrevEnabled = enabled;
        }

        bool contextOrConfigChanged = RefreshContext() || config::Refresh();

        // Re-read boost state after RefreshContext().
        //
        // RefreshContext() calls WaitForNextTick() (a full polling-interval
        // sleep, typically 300 ms) whenever an app or profile change is
        // detected — which is exactly what happens at a loading-screen
        // transition.  Boost mode almost always starts during that sleep.
        // If we kept the isBoost value read at the top of Tick(), SetClocks()
        // would see isBoost=false even though boost is now active, and would
        // push the CPU to the plain profile clock (X) for one whole tick:
        //
        //   X  →  ~1785 (APM boost)  →  X  (stale isBoost=false)
        //   →  2600  (next tick, isBoost re-read correctly)  →  X
        //
        // Refreshing here collapses that to:
        //
        //   X  →  ~1785 (APM boost)  →  2600  →  X
        //
        rc = apmExtGetCurrentPerformanceConfiguration(&mode);
        ASSERT_RESULT_OK(rc, "apmExtGetCurrentPerformanceConfiguration (post-refresh)");
        isBoost = apmExtIsBoostMode(mode);

        // Detect boost state transitions so we can force a SetClocks call the
        // instant boost starts or ends, even if RefreshContext hasn't caught up
        // with the hardware clock change yet.
        bool boostChanged = (isBoost != gPrevIsBoost);
        gPrevIsBoost = isBoost;

        // While boost+OverwriteBoostMode is active, force SetClocks every tick.
        // APM can re-assert the default boost clock (~1785 MHz) between our
        // polling intervals; actively re-applying each tick ensures we reclaim
        // the configured target within one tick interval rather than waiting for
        // RefreshContext to detect the hardware deviation on a future tick.
        // The nearestHz != gContext.freqs guard inside SetClocks prevents
        // unnecessary board::SetHz calls when the clock is already correct.
        bool overrideBoostActive = isBoost && (bool)config::GetConfigValue(HocClkConfigValue_OverwriteBoostMode);

        if (!enabled) {
            // Disabled: reset to stock on the first disabled tick and again
            // on any context change (app/profile switch, override removed,
            // etc.) so OC clocks don't persist while the module is off.
            if (enabledChanged || contextOrConfigChanged) {
                board::ResetToStock();
            }
        } else {
            // Enabled: apply configured clocks when (re-)enabled, on any
            // context/config change, on boost state transitions, or on every
            // tick while an active boost override needs to be maintained.
            if (enabledChanged || contextOrConfigChanged || boostChanged || overrideBoostActive) {
                SetClocks(isBoost);
            }
        }

        // Publish completed context to the IPC-visible snapshot under a brief lock.
        // GetCurrentContext() only touches gContextSnapshot, so it never blocks on heavy work.
        {
            std::scoped_lock lock{gSnapshotMutex};
            gContextSnapshot = gContext;
        }
    }

    void WaitForNextTick()
    {
        // Always use the configured polling interval.
        //
        // The original code slept for 5 seconds when mem ≤ 665 MHz, intending to
        // detect Switch sleep/suspend mode.  In practice the condition fires any time
        // the sysmodule starts before a game clock profile is applied, or when the
        // config targets 665 MHz — causing multi-second stalls in the tick loop that
        // make the overlay stall and clocks stop being managed.
        //
        // 300 ms ticks even during genuine sleep mode are harmless: nothing changes
        // while the screen is off, so the tick just re-reads the same state and sleeps
        // again.  The battery cost of ~200 extra wakeups per minute is negligible.
        svcSleepThread(config::GetConfigValue(HocClkConfigValue_PollingIntervalMs) * 1000000ULL);
    }
} // namespace clockManager