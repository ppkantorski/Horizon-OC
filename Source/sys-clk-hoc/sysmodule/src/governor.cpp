/*
 * Copyright (c) Souldbminer and Horizon OC Contributors
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

#include "governor.hpp"
#include "process_management.hpp"
#include <hocclk/clock_manager.h>
namespace governor {

    #define POLL_NS 5'000'000       // 5 ms  – governor poll rate
    #define DOWN_HOLD_TICKS 10      // 50 ms – how long to hold in POLL_NS while ramping down
    #define STEP_UTIL 900           // multiplier for step calculations

    bool isGpuGovernorEnabled = false;
    bool isCpuGovernorEnabled = false;
    bool lastGpuGovernorState = false;
    bool lastCpuGovernorState = false;
    bool lastVrrGovernorState = false;
    bool hasChanged = true;
    bool isCpuGovernorInBoostMode = false;
    bool isVRREnabled = false;

    // Single merged governor thread (CPU + GPU + VRR)
    Thread governorTHREAD;

    void HandleGovernor(uint32_t targetHz)
    {
        // If governing is globally disabled, deactivate all governors and bail.
        if (!config::GetConfigValue(HocClkConfigValue_AllowGoverning)) {
            if (isCpuGovernorEnabled) { board::ResetToStockCpu(); }
            if (isGpuGovernorEnabled) { board::ResetToStockGpu(); }
            isCpuGovernorEnabled  = false;
            isGpuGovernorEnabled  = false;
            isVRREnabled          = false;
            lastCpuGovernorState  = false;
            lastGpuGovernorState  = false;
            lastVrrGovernorState  = false;
            return;
        }

        u32 tempTargetHz = clockManager::gContext.overrideFreqs[HocClkModule_Governor];
        if (!tempTargetHz) {
            tempTargetHz = config::GetAutoClockHz(clockManager::gContext.applicationId, HocClkModule_Governor, clockManager::gContext.profile, true);
            if (!tempTargetHz)
                tempTargetHz = config::GetAutoClockHz(HOCCLK_GLOBAL_PROFILE_TID, HocClkModule_Governor, clockManager::gContext.profile, true);
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
        isVRREnabled         = newVrrGovernorState;

        // No 100ms sync sleeps needed — everything runs in one thread now.
        if (newCpuGovernorState == false && lastCpuGovernorState == true)
            board::ResetToStockCpu();
        if (newGpuGovernorState == false && lastGpuGovernorState == true)
            board::ResetToStockGpu();
        if (newVrrGovernorState == false && lastVrrGovernorState == true)
            board::ResetToStockDisplay();

        if (newCpuGovernorState != lastCpuGovernorState || newGpuGovernorState != lastGpuGovernorState || newVrrGovernorState != lastVrrGovernorState) {
            fileUtils::LogLine("[mgr] Governor state changed: CPU %s, GPU %s, VRR %s",
                newCpuGovernorState ? "enabled" : "disabled",
                newGpuGovernorState ? "enabled" : "disabled",
                newVrrGovernorState ? "enabled" : "disabled");
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

    u32 ResolveTargetHz(HocClkModule module)
    {
        u32 hz = clockManager::gContext.overrideFreqs[module];
        if (!hz)
            hz = config::GetAutoClockHz(
                    clockManager::gContext.applicationId, module,
                    clockManager::gContext.profile, false);
        if (!hz)
            hz = config::GetAutoClockHz(
                    HOCCLK_GLOBAL_PROFILE_TID, module,
                    clockManager::gContext.profile, false);
        return hz;
    }

    // Merged governor thread: handles CPU, GPU, and VRR in one loop.
    // Previously three separate threads; merged to save two thread stacks
    // (~16 KB each) and eliminate the 100 ms sync sleeps that were needed
    // to sequence state transitions across threads.
    void GovernorThread(void* arg)
    {
        (void)arg;

        u32 cpuDownHoldRemaining = 0;
        u32 cpuLastHz            = 0;
        u32 gpuDownHoldRemaining = 0;
        u32 gpuLastHz            = 0;
        u32 cpuMinHz             = 0;   // refreshed every governor tick from config
        u8  vrrFocusTick         = 0;
        u8  vrrTick              = 0;
        bool cpuWasInBoost       = false; // tracks previous boost state for exit-edge detection

        for (;;) {
            if (!clockManager::gRunning) {
                svcSleepThread(POLL_NS);
                continue;
            }

            // ── CPU governor ──────────────────────────────────────────────
            //
            // Read APM mode once here — used both by the governor scaling path
            // and the OverwriteBoostMode maintenance block below.
            u32 cpuApmMode = 0;
            bool cpuInBoost = false;
            {
                Result rc = apmExtGetCurrentPerformanceConfiguration(&cpuApmMode);
                if (R_SUCCEEDED(rc))
                    cpuInBoost = apmExtIsBoostMode(cpuApmMode);
            }

            // Detect the falling edge of boost (first non-boost tick after boost was active).
            // Used to run one trailing-edge OverwriteBoostMode correction and to signal the
            // tick thread so it arms the debounce immediately rather than waiting up to 50 ms.
            bool cpuJustExitedBoost = (!cpuInBoost && cpuWasInBoost);
            cpuWasInBoost = cpuInBoost;

            // ── Boost-exit debounce arming (early) ──────────────────────────
            // MUST happen before the isCpuGovernorEnabled scaling block below.
            // On the tick where cpuJustExitedBoost first becomes true, the
            // non-boost scaling path reads s_boostExitDeadlineNs to decide
            // whether to suppress scaling.  If arming were deferred until after
            // the scaling block (the old placement), s_boostExitDeadlineNs would
            // still be 0 on this tick → inDebounce=false → governor scales to
            // 612 MHz → debounce arms one tick too late.  Moving arming here
            // ensures the deadline is set before the scaling check reads it.
            //
            // Also covers the Mariko DVFS settle sleep (300 ms): the tick thread
            // releases gContextMutex for the sleep and cannot arm the debounce
            // until it wakes.  The governor runs every 1–5 ms during that window;
            // arming here ensures the scaling path is suppressed immediately.
            if (cpuJustExitedBoost) {
                std::scoped_lock armLock{clockManager::gContextMutex};
                if (clockManager::s_boostExitDeadlineNs == 0) {
                    clockManager::s_boostExitDeadlineNs =
                        armTicksToNs(armGetSystemTick()) + clockManager::kBoostExitHoldNs;
                }
            }

            if (isCpuGovernorEnabled) {
                if (cpuInBoost) {
                    // Signal the tick thread on the FIRST boost entry so it wakes
                    // immediately (< 1 ms) instead of waiting up to 300 ms for its
                    // normal polling interval.  Without this, there is a window after
                    // boost starts where the governor has already stepped back but the
                    // tick thread hasn't yet applied the boost profile clock — causing
                    // a brief period at whatever frequency the governor left behind.
                    // One signal per boost entry is enough; the guard prevents spamming
                    // leventSignal every 5 ms while boost is continuously held.
                    if (!isCpuGovernorInBoostMode)
                        leventSignal(&clockManager::gTickWakeEvent);

                    isCpuGovernorInBoostMode = true;
                    cpuDownHoldRemaining     = 0;
                    cpuLastHz                = 0;
                    // OverwriteBoostMode clock applied by the block below.
                } else {
                    isCpuGovernorInBoostMode = false;

                    auto& cpuTable = clockManager::gFreqTable[HocClkModule_CPU];
                    if (cpuTable.count > 0) {
                        std::scoped_lock lock{clockManager::gContextMutex};

                        // Suppress CPU scaling during the boost-exit debounce window.
                        //
                        // When TotK pulses APM boost on/off rapidly during loading, the
                        // brief non-boost gaps make cpuInBoost=false for a few ms.  The
                        // tick thread's SetClocks skips CPU via skipCpuForDebounce during
                        // this window, but the governor has no such guard — it sees non-boost,
                        // reads near-zero CPU load (loading screens are IO-bound), and drives
                        // the CPU straight to 612 MHz.  Nobody corrects it back until the
                        // next boost re-entry + tick-thread cycle (up to 50 ms later).
                        //
                        // s_boostExitDeadlineNs is armed by the tick thread on boost exit
                        // and also by the governor itself (cpuJustExitedBoost arming block
                        // above, before this scaling section) for coverage during the DVFS
                        // settle sleep.  Both writers hold gContextMutex.
                        u64 nowNs = armTicksToNs(armGetSystemTick());
                        bool inDebounce = (clockManager::s_boostExitDeadlineNs != 0 &&
                                           nowNs < clockManager::s_boostExitDeadlineNs);
                        if (inDebounce) {
                            // Hold off — reset state so there's no stale momentum
                            // when the debounce expires and scaling resumes.
                            cpuDownHoldRemaining = 0;
                            cpuLastHz            = 0;
                        } else {

                        u32 cpuLoad    = board::GetPartLoad(HocClkPartLoad_CPUMax);
                        u32 tableMaxHz = cpuTable.list[cpuTable.count - 1];
                        u32 targetHz   = ResolveTargetHz(HocClkModule_CPU);
                        u32 maxHz      = clockManager::GetMaxAllowedHz(HocClkModule_CPU, clockManager::gContext.profile);

                        // When no explicit CPU clock is set ("do not override"), cap the
                        // SchedutilTargetHz reference to stock APM CPU frequency (1020 MHz).
                        // Without this, the schedutil curve scales against tableMaxHz (the
                        // OC hardware table max, e.g. 2295 MHz on Mariko), driving the
                        // governor above stock even at moderate load — e.g. 45% load
                        // targets 1147 MHz instead of ~567 MHz, providing no battery saving.
                        // 1020 MHz is the correct stock for all non-boost APM modes.
                        // Boost is handled by the tick thread; the governor does not run
                        // during it.  If the user wants above-stock OC, they should set an
                        // explicit CPU clock in the profile — targetHz will then be non-zero
                        // and this fallback will not apply.
                        static constexpr u32 kStockCpuHz = 1020000000u;
                        u32 scaleMax  = targetHz ? targetHz : kStockCpuHz;
                        u32 desiredHz = SchedutilTargetHz(cpuLoad, scaleMax);

                        if (targetHz && desiredHz > targetHz) desiredHz = targetHz;
                        if (maxHz    && desiredHz > maxHz)    desiredHz = maxHz;

                        u32 newHz = cpuTable.list[TableIndexForHz(cpuTable, desiredHz)];
                        bool goingDown = (cpuLastHz != 0) && (newHz < cpuLastHz);

                        if (!goingDown) {
                            cpuDownHoldRemaining = 0;
                        } else if (cpuDownHoldRemaining == 0) {
                            // Asymmetric hold: scale by drop magnitude relative to table max.
                            // A tiny step (1785→1632) gets ~1 tick (5ms) — low risk, respond fast.
                            // A large step (2295→612) gets up to DOWN_HOLD_TICKS (50ms) — high
                            // risk, hold longer to avoid a costly stutter if load spikes back.
                            // Minimum 1 tick so even a tiny drop gets at least one hold cycle.
                            // Overflow-safe: promote to u64 before multiply since DOWN_HOLD_TICKS
                            // * dropHz can exceed u32 for large drops at high frequencies.
                            u32 dropHz = cpuLastHz - newHz;
                            cpuDownHoldRemaining = (u32)std::max((u32)DOWN_HOLD_TICKS / 2,
                                (u32)((u64)DOWN_HOLD_TICKS * dropHz / tableMaxHz));
                        }

                        if (cpuDownHoldRemaining > 0)
                            cpuDownHoldRemaining--;

                        // Read minimum floor every governor tick so it stays in sync with
                        // the tick thread. Previously re-read every 250ms (50 ticks × 5ms),
                        // meaning a user change via the overlay could be ignored for up to
                        // 250ms while the tick thread picked it up in under 1ms via levent.
                        cpuMinHz = config::GetConfigValue(HocClkConfigValue_CpuGovernorMinimumFreq);

                        if (newHz < cpuMinHz)
                            newHz = cpuMinHz;

                        if ((!goingDown || (cpuDownHoldRemaining == 0)) && clockManager::IsAssignableHz(HocClkModule_CPU, newHz)) {
                            board::SetHz(HocClkModule_CPU, newHz);
                            clockManager::gContext.freqs[HocClkModule_CPU] = newHz;
                            cpuLastHz = newHz;
                        }
                        } // end !inDebounce
                    }
                }
            } else {
                cpuDownHoldRemaining = 0;
                cpuLastHz            = 0;
            }

            // ── OverwriteBoostMode CPU maintenance ─────────────────────────
            // When OverwriteBoostMode is active the tick thread applies the
            // configured boost clock every 50 ms.  APM re-asserts its own
            // boost clock (1785 MHz) each time the game calls
            // apmSetPerformanceConfiguration, so the CPU can sit at 1785 for
            // up to 50 ms between our corrections — producing visible flicker.
            //
            // The governor loop runs every 5 ms.  By re-applying the target
            // here we reduce the APM re-assertion window from 50 ms to 5 ms,
            // making the flicker effectively imperceptible.
            //
            // Also covers the boost EXIT edge (cpuJustExitedBoost):
            //   • Runs one trailing correction to push the clock back to the
            //     boost target even though cpuInBoost is now false.  This
            //     prevents the brief 1785 window that appears during APM's
            //     own boost→non-boost transition from being held.
            //   • Signals the tick thread so it arms the debounce within 1 ms
            //     (vs. waiting up to 50 ms on the fast-tick interval) — the
            //     debounce then holds the boost clock through any remaining
            //     TotK-style rapid pulse gaps cleanly.
            //
            // Runs regardless of isCpuGovernorEnabled so it works whether or
            // not the CPU governor is active.  The board::GetHz guard prevents
            // redundant SetHz calls when the clock is already correct.
            if ((cpuInBoost || cpuJustExitedBoost) && config::GetConfigValue(HocClkConfigValue_OverwriteBoostMode)) {
                // Signal tick thread on the first non-boost tick so it arms the
                // debounce immediately.  One signal per boost-exit edge; the
                // cpuJustExitedBoost flag is only true for a single governor tick.
                if (cpuJustExitedBoost) {
                    leventSignal(&clockManager::gTickWakeEvent);
                }

                auto& cpuTable = clockManager::gFreqTable[HocClkModule_CPU];
                if (cpuTable.count > 0) {
                    std::scoped_lock lock{clockManager::gContextMutex};
                    u32 targetHz = ResolveTargetHz(HocClkModule_CPU);
                    if (targetHz) {
                        u32 maxHz = clockManager::GetMaxAllowedHz(HocClkModule_CPU, clockManager::gContext.profile);
                        if (maxHz && targetHz > maxHz) targetHz = maxHz;
                        u32 nearestHz = cpuTable.list[TableIndexForHz(cpuTable, targetHz)];
                        // IMPORTANT: compare against board::GetHz (live hardware read), NOT
                        // gContext.freqs (cache).  The tick thread writes gContext.freqs=2601
                        // after applying OverwriteBoostMode, but APM can re-assert its own
                        // boost clock (1785 MHz) at any time.  When it does, the hardware is
                        // at 1785 while gContext.freqs still says 2601 — so the cache-based
                        // check (nearestHz != gContext.freqs) evaluates 2601 != 2601 = false,
                        // silently skipping the correction.  APM then holds 1785 for up to
                        // 50 ms until the tick thread detects the mismatch via its own freq
                        // loop, producing the visible 2601→1785→2601→1785 oscillation.
                        // Using board::GetHz here catches every re-assertion within 5 ms.
                        u32 actualHz = board::GetHz(HocClkModule_CPU);
                        if (nearestHz != actualHz) {
                            board::SetHz(HocClkModule_CPU, nearestHz);
                            clockManager::gContext.freqs[HocClkModule_CPU] = nearestHz;
                        }
                    }
                }
            }

            // ── GPU governor ──────────────────────────────────────────────
            if (isGpuGovernorEnabled) {
                auto& gpuTable = clockManager::gFreqTable[HocClkModule_GPU];
                if (gpuTable.count > 0) {
                    std::scoped_lock lock{clockManager::gContextMutex};

                    u32 gpuLoad    = board::GetPartLoad(HocClkPartLoad_GPU);
                    u32 tableMaxHz = gpuTable.list[gpuTable.count - 1];
                    u32 desiredHz  = SchedutilTargetHz(gpuLoad, tableMaxHz);
                    u32 targetHz   = ResolveTargetHz(HocClkModule_GPU);
                    u32 maxHz      = clockManager::GetMaxAllowedHz(HocClkModule_GPU, clockManager::gContext.profile);

                    if (targetHz && desiredHz > targetHz) desiredHz = targetHz;
                    if (maxHz    && desiredHz > maxHz)    desiredHz = maxHz;

                    u32 newHz = gpuTable.list[TableIndexForHz(gpuTable, desiredHz)];
                    bool goingDown = (gpuLastHz != 0) && (newHz < gpuLastHz);

                    if (!goingDown) {
                        gpuDownHoldRemaining = 0;
                    } else if (gpuDownHoldRemaining == 0) {
                        // Same asymmetric hold logic as CPU: proportional to drop magnitude.
                        // Small GPU step → short hold, large GPU step → longer hold.
                        u32 dropHz = gpuLastHz - newHz;
                        gpuDownHoldRemaining = (u32)std::max((u32)DOWN_HOLD_TICKS / 2,
                            (u32)((u64)DOWN_HOLD_TICKS * dropHz / tableMaxHz));
                    }

                    if (gpuDownHoldRemaining > 0)
                        gpuDownHoldRemaining--;

                    if ((!goingDown || (gpuDownHoldRemaining == 0)) && clockManager::IsAssignableHz(HocClkModule_GPU, newHz)) {
                        board::SetHz(HocClkModule_GPU, newHz);
                        clockManager::gContext.freqs[HocClkModule_GPU] = newHz;
                        gpuLastHz = newHz;
                    }
                }
            } else {
                gpuDownHoldRemaining = 0;
                gpuLastHz            = 0;
            }

            // ── VRR governor ──────────────────────────────────────────────
            if (isVRREnabled && clockManager::gContext.profile != HocClkProfile_Docked
                             && clockManager::gContext.isSaltyNXInstalled) {
                bool skipVrr = false;

                if (++vrrFocusTick > 100) {
                    vrrFocusTick = 0;
                    bool isApplicationOutOfFocus = false;
                    Result rc = processManagement::isApplicationOutOfFocus(&isApplicationOutOfFocus);
                    if (R_FAILED(rc) || isApplicationOutOfFocus) {
                        board::ResetToStockDisplay();
                        skipVrr = true;
                    }
                }

                if (!skipVrr) {
                    u8 fps = integrations::GetSaltyNXFPS();

                    if (fps != 254) {
                        std::scoped_lock lock{clockManager::gContextMutex};

                        u32 targetHz = clockManager::gContext.overrideFreqs[HocClkModule_Display];
                        if (!targetHz) {
                            targetHz = config::GetAutoClockHz(clockManager::gContext.applicationId, HocClkModule_Display, clockManager::gContext.profile, false);
                            if (!targetHz)
                                targetHz = config::GetAutoClockHz(HOCCLK_GLOBAL_PROFILE_TID, HocClkModule_Display, clockManager::gContext.profile, false);
                        }

                        u8 maxDisplay = targetHz ? (u8)targetHz : 60;
                        u8 minDisplay = board::GetConsoleType() == HocClkConsoleType_Aula ? 45 : 40;

                        if (maxDisplay != minDisplay) {
                            if (fps >= minDisplay && fps <= maxDisplay) {
                                board::SetHz(HocClkModule_Display, fps);
                                clockManager::gContext.freqs[HocClkModule_Display] = fps;
                                clockManager::gContext.realFreqs[HocClkModule_Display] = fps;
                            } else {
                                for (u32 i = 0; i < 10; i++) {
                                    u32 compareHz = fps * i;
                                    if (compareHz >= minDisplay && compareHz <= maxDisplay) {
                                        board::SetHz(HocClkModule_Display, compareHz);
                                        clockManager::gContext.freqs[HocClkModule_Display] = compareHz;
                                        clockManager::gContext.realFreqs[HocClkModule_Display] = compareHz;
                                        break;
                                    }
                                }
                            }

                            if (++vrrTick > 50) {
                                vrrTick = 0;
                                board::SetHz(HocClkModule_Display, maxDisplay);
                                svcSleepThread(50'000'000);
                            }
                        }
                    }
                }
            }

            // Dynamic poll rate: use 1 ms when OverwriteBoostMode is active and the
            // CPU is in boost (or just exited boost for the trailing correction tick).
            // APM can re-assert its own boost clock (1785 MHz) between governor polls;
            // at 5 ms this produces a visible blip on any monitoring tool.  At 1 ms
            // the window shrinks to at most 1 ms — imperceptible in practice.
            //
            // Why not a levent here instead?  The governor detects boost FASTER than
            // the tick thread (5 ms vs 50 ms polling), so there is nothing upstream
            // that could signal the governor earlier than it would detect on its own.
            // The existing gTickWakeEvent signals (governor→tick on boost entry/exit)
            // are already the correct direction.  A dynamic sleep is the only lever
            // that reduces the first-correction latency here.
            //
            // The 1 ms rate is only active when OverwriteBoostMode needs it; all
            // other governor work (CPU scaling, GPU governor, VRR) stays at 5 ms.
            bool needsFastPoll = (cpuInBoost || cpuJustExitedBoost) &&
                                 (bool)config::GetConfigValue(HocClkConfigValue_OverwriteBoostMode);
            svcSleepThread(needsFastPoll ? 1'000'000ULL : POLL_NS);
        }
    }

    void startThreads() {
        threadCreate(
            &governorTHREAD,
            GovernorThread,
            nullptr,
            NULL,
            0x2000,
            0x3F,
            -2
        );
        threadStart(&governorTHREAD);
    }

    void exitThreads() {
        threadClose(&governorTHREAD);
    }
}