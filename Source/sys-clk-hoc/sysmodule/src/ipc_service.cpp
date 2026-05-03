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
 */

/*
 * IPC compatibility bridge: exposes "sys:clk" service with API v4 so that
 * the standard sys-clk develop overlay works unmodified, while the sysmodule
 * internally keeps all HOC extended functionality.
 *
 * KIP commands (12/13) removed — the sysmodule never touches the KIP.
 *
 * DISPATCH FIX (root cause of previous failure):
 *
 *   The sys-clk overlay client (common/src/client/ipc.c) uses:
 *     serviceDispatchOut    for GetCurrentContext (cmd 2)   → raw output
 *     serviceDispatchInOut  for GetProfiles       (cmd 5)   → raw in/out
 *     serviceDispatchOut    for GetConfigValues   (cmd 9)   → raw output
 *     serviceDispatchIn     for SetConfigValues   (cmd 10)  → raw input
 *
 *   All four use the HIPC raw data section, NOT hipc buffers.
 *   The previous version checked num_recv_buffers / num_send_buffers for
 *   these four commands. The overlay never sends those buffers, so every
 *   check failed and returned HOCCLK_ERROR(Generic) = 0x184.
 *
 *   Fix: cmds 2, 5, 9, 10 now read/write through out_data / r->data.ptr.
 *
 *   Cmds 1 and 11 still use hipc buffers (HipcMapAlias and HipcAutoSelect)
 *   and were already correct.
 */

#include "ipc_service.hpp"
#include <cstring>
#include <switch.h>
#include <nxExt.h>
#include "file_utils.hpp"
#include "errors.hpp"
#include "clock_manager.hpp"
#include "config.hpp"

// ---------------------------------------------------------------------------
// Service identity
// ---------------------------------------------------------------------------
#define SYSCLK_IPC_SERVICE_NAME  "sys:clk"
#define SYSCLK_IPC_API_VERSION   4

enum SysClkIpcCmd
{
    SysClkIpcCmd_GetApiVersion     = 0,
    SysClkIpcCmd_GetVersionString  = 1,
    SysClkIpcCmd_GetCurrentContext = 2,
    SysClkIpcCmd_Exit              = 3,
    SysClkIpcCmd_GetProfileCount   = 4,
    SysClkIpcCmd_GetProfiles       = 5,
    SysClkIpcCmd_SetProfiles       = 6,
    SysClkIpcCmd_SetEnabled        = 7,
    SysClkIpcCmd_SetOverride       = 8,
    SysClkIpcCmd_GetConfigValues   = 9,
    SysClkIpcCmd_SetConfigValues   = 10,
    SysClkIpcCmd_GetFreqList       = 11,
};

// ---------------------------------------------------------------------------
// Wire types matching sys-clk overlay's common/include/sysclk/ headers.
// Defined here to avoid any dependency on overlay headers.
// ---------------------------------------------------------------------------
#define SYSCLK_MODULE_ENUMMAX   3   // CPU=0, GPU=1, MEM=2
#define SYSCLK_PROFILE_ENUMMAX  5   // Handheld..Docked
#define SYSCLK_CONFIG_ENUMMAX   5   // PollingIntervalMs..CsvWriteIntervalMs

// SysClkContext: sizeof = 116 bytes
typedef struct {
    uint8_t  enabled;                              // +0
    // 7 bytes compiler padding (uint64_t alignment)
    uint64_t applicationId;                        // +8
    uint32_t profile;                              // +16
    uint32_t freqs[SYSCLK_MODULE_ENUMMAX];         // +20
    uint32_t realFreqs[SYSCLK_MODULE_ENUMMAX];     // +32
    uint32_t overrideFreqs[SYSCLK_MODULE_ENUMMAX]; // +44
    uint32_t temps[3];                             // +56 (SOC,PCB,Skin)
    int32_t  power[2];                             // +68 (Now,Avg)
    uint32_t ramLoad[2];                           // +76 (All,CPU)
    uint32_t realVolts[4];                         // +84 (CPU, GPU, packed_VDD2_VDDQ, SOC)
    // HOC extension: per-component die temperatures (milliCelsius)
    uint32_t componentTemps[3];                    // +100 (CPU die, GPU die, MEM/PLLX)
    // HOC extension: temporary governor override packed value
    // bits 7:0 = CPU governor, bits 15:8 = GPU governor
    // (0=DoNotOverride, 1=Disabled, 2=Enabled per component)
    uint32_t governorOverride;                     // +112
                                                   // total = 116
} SysClkContext_Wire;

// SysClkTitleProfileList: 5*3*4 = 60 bytes
typedef struct {
    union {
        uint32_t mhz[SYSCLK_PROFILE_ENUMMAX * SYSCLK_MODULE_ENUMMAX];
        uint32_t mhzMap[SYSCLK_PROFILE_ENUMMAX][SYSCLK_MODULE_ENUMMAX];
    };
} SysClkTitleProfileList_Wire;

// SysClkConfigValueList: 5*8 = 40 bytes
typedef struct {
    uint64_t values[SYSCLK_CONFIG_ENUMMAX];
} SysClkConfigValueList_Wire;

typedef struct {
    uint32_t module;
    uint32_t hz;
} SysClkIpc_SetOverride_Args_Wire;

typedef struct {
    uint64_t tid;
    SysClkTitleProfileList_Wire profiles;
} SysClkIpc_SetProfiles_Args_Wire;

typedef struct {
    uint32_t module;
    uint32_t maxCount;
} SysClkIpc_GetFreqList_Args_Wire;

// ---------------------------------------------------------------------------
// HOC → wire translation
// ---------------------------------------------------------------------------

static SysClkContext_Wire TranslateContext(const HocClkContext& hoc)
{
    SysClkContext_Wire ctx = {};
    ctx.enabled       = config::Enabled() ? 1u : 0u;
    ctx.applicationId = hoc.applicationId;
    ctx.profile       = (uint32_t)hoc.profile;

    for (int i = 0; i < SYSCLK_MODULE_ENUMMAX; i++) {
        ctx.freqs[i]         = hoc.freqs[i];
        ctx.realFreqs[i]     = hoc.realFreqs[i];
        ctx.overrideFreqs[i] = hoc.overrideFreqs[i];
    }

    ctx.temps[0] = hoc.temps[HocClkThermalSensor_SOC];
    ctx.temps[1] = hoc.temps[HocClkThermalSensor_PCB];
    ctx.temps[2] = hoc.temps[HocClkThermalSensor_Skin];

    // Per-component die temperatures for the overlay's HOC temp-row feature.
    // MEM uses the PLLX sensor on Mariko (closest proxy for memory die temp).
    ctx.componentTemps[0] = hoc.temps[HocClkThermalSensor_CPU];
    ctx.componentTemps[1] = hoc.temps[HocClkThermalSensor_GPU];
    ctx.componentTemps[2] = hoc.temps[HocClkThermalSensor_MEM];
    ctx.governorOverride  = hoc.overrideFreqs[HocClkModule_Governor];

    ctx.power[0] = hoc.power[HocClkPowerSensor_Now];
    ctx.power[1] = hoc.power[HocClkPowerSensor_Avg];

    // Status Monitor reads ramLoad[]/10 to display RAM utilization as a percentage.
    // HocClkPartLoad_EMC/EMCCpu return per-mille utilization (0-1000 == 0.0-100.0%),
    // which divides cleanly by 10 to yield the correct 0-100% display value.
    // The old RamBWAll/RamBWCpu values are raw bandwidth (MB/s), not a percentage,
    // which caused the overlay to show e.g. 119% instead of the actual ~9%.
    ctx.ramLoad[0] = hoc.partLoad[HocClkPartLoad_EMC];
    ctx.ramLoad[1] = hoc.partLoad[HocClkPartLoad_EMCCpu];

    ctx.realVolts[0] = hoc.voltages[HocClkVoltage_CPU];
    ctx.realVolts[1] = hoc.voltages[HocClkVoltage_GPU];
    ctx.realVolts[3] = hoc.voltages[HocClkVoltage_SOC];

    // Pack VDD2 (EMCVDD2) and VDDQ (EMCVDDQ) into realVolts[2] for the overlay.
    // Overlay unpack: vdd2_mV = packed / 100000.0f, vddq_mV = (packed % 10000) / 10
    // Voltages from HOC are in µV.  On Erista EMCVDDQ == EMCVDD2 (~1125 mV) which
    // would overflow the 4-digit VDDQ field, so we only pack VDDQ on Mariko where
    // it is a separate lower-voltage rail (~550–650 mV).
    {
        const float   vdd2_mV  = hoc.voltages[HocClkVoltage_EMCVDD2] / 1000.0f;
        const uint32_t vddq_mV = hoc.voltages[HocClkVoltage_EMCVDDQ] / 1000;
        uint32_t packed = (uint32_t)(vdd2_mV * 100000.0f);
        if (vddq_mV < 1000) {   // Mariko: VDDQ ~550–650 mV; Erista returns same as VDD2
            packed += vddq_mV * 10;
        }
        ctx.realVolts[2] = packed;
    }

    return ctx;
}

static HocClkTitleProfileList ToHocProfileList(const SysClkTitleProfileList_Wire& wire, std::uint64_t tid)
{
    HocClkTitleProfileList hoc = {};
    // Load the current stored profile first so Governor (and Display) are preserved.
    // Without this, every SetProfiles call from the overlay would zero out Governor.
    config::GetProfiles(tid, &hoc);
    // Overwrite only CPU / GPU / MEM from the wire format.
    for (int p = 0; p < SYSCLK_PROFILE_ENUMMAX; p++) {
        hoc.mhzMap[p][HocClkModule_CPU] = wire.mhzMap[p][0];
        hoc.mhzMap[p][HocClkModule_GPU] = wire.mhzMap[p][1];
        hoc.mhzMap[p][HocClkModule_MEM] = wire.mhzMap[p][2];
    }
    return hoc;
}

static SysClkTitleProfileList_Wire ToWireProfileList(const HocClkTitleProfileList& hoc)
{
    SysClkTitleProfileList_Wire wire = {};
    for (int p = 0; p < SYSCLK_PROFILE_ENUMMAX; p++) {
        wire.mhzMap[p][0] = hoc.mhzMap[p][HocClkModule_CPU];
        wire.mhzMap[p][1] = hoc.mhzMap[p][HocClkModule_GPU];
        wire.mhzMap[p][2] = hoc.mhzMap[p][HocClkModule_MEM];
    }
    return wire;
}

// ---------------------------------------------------------------------------
// IPC service
// ---------------------------------------------------------------------------

namespace ipcService {

    namespace {

        bool gRunning = false;
        Thread gThread;
        LockableMutex gThreadMutex;
        IpcServer gServer;

        Result GetApiVersion(u32* out_version)
        {
            *out_version = SYSCLK_IPC_API_VERSION;
            return 0;
        }

        Result GetVersionString(char* out_buf, size_t bufSize)
        {
            if (bufSize) {
                strncpy(out_buf, TARGET_VERSION, bufSize - 1);
                out_buf[bufSize - 1] = '\0';
            }
            return 0;
        }

        Result GetCurrentContext(SysClkContext_Wire* out_ctx)
        {
            HocClkContext hoc = clockManager::GetCurrentContext();
            *out_ctx = TranslateContext(hoc);
            return 0;
        }

        Result ExitHandler()
        {
            // Run the cleanup BEFORE signaling the main loop to exit. This
            // preserves the ordering of the original handler — the loop and
            // the IPC service both stay fully alive while we do svc work
            // (PCV restore, GPU bounce). Setting gRunning=false first would
            // let the main thread start tearing down ipcService while our
            // svc calls are still in flight, racing the server teardown.
            //
            // Sequence guarantee:
            //   1. PrepareForShutdown completes (PCV is clean, GPU bounced)
            //   2. SetRunning(false) flips the flag
            //   3. We return; ipc framework sends the reply to ovlSysmodules
            //   4. Main loop's next iteration sees gRunning=false, exits,
            //      runs Exit() which is idempotent for our work
            //   5. ovlSysmodules either sees the process exit naturally (poll
            //      hits PID-not-found) or, after timeout, force-kills it.
            //      Either way, PCV was already restored in step 1.
            clockManager::PrepareForShutdown();
            clockManager::SetRunning(false);
            return 0;
        }

        Result GetProfileCount(std::uint64_t* tid, std::uint8_t* out_count)
        {
            if (!config::HasProfilesLoaded()) {
                return HOCCLK_ERROR(ConfigNotLoaded);
            }
            *out_count = config::GetProfileCount(*tid);
            return 0;
        }

        Result GetProfiles(std::uint64_t* tid, SysClkTitleProfileList_Wire* out_profiles)
        {
            if (!config::HasProfilesLoaded()) {
                return HOCCLK_ERROR(ConfigNotLoaded);
            }
            HocClkTitleProfileList hocProfiles;
            config::GetProfiles(*tid, &hocProfiles);
            *out_profiles = ToWireProfileList(hocProfiles);
            return 0;
        }

        Result SetProfiles(SysClkIpc_SetProfiles_Args_Wire* args)
        {
            if (!config::HasProfilesLoaded()) {
                return HOCCLK_ERROR(ConfigNotLoaded);
            }
            // Force a full config reload before merging profiles.
            //
            // The overlay writes governor packed values (e.g. handheld_governor=2)
            // directly to config.ini without going through IPC SetProfiles.
            // FAT mtime has 2-second granularity, so the normal Refresh() cycle
            // may not have picked up the write yet.  Without this call,
            // ToHocProfileList reads a stale in-memory cache with governor=0,
            // then ini_putsection rewrites the TID section and erases the key.
            config::ForceRefresh();
            HocClkTitleProfileList hocProfiles = ToHocProfileList(args->profiles, args->tid);
            if (!config::SetProfiles(args->tid, &hocProfiles, true)) {
                return HOCCLK_ERROR(ConfigSaveFailed);
            }
            // Trigger immediate SetClocks() on the next tick — the overlay
            // calls SetProfiles after writing governor directly to disk, so
            // the tick loop must not wait for FAT mtime to advance.
            config::MarkConfigDirty();
            return 0;
        }

        Result SetEnabled(std::uint8_t* enabled)
        {
            config::SetEnabled(*enabled != 0);
            return 0;
        }

        Result SetOverride(SysClkIpc_SetOverride_Args_Wire* args)
        {
            // Allow CPU(0), GPU(1), MEM(2), and Governor(3) overrides.
            // Display(4) is driven by VRR and is not settable via this IPC bridge.
            if (args->module > HocClkModule_Governor) {
                return HOCCLK_ERROR(Generic);
            }
            config::SetOverrideHz((HocClkModule)args->module, args->hz);
            return 0;
        }

        Result GetConfigValuesHandler(SysClkConfigValueList_Wire* out_configValues)
        {
            if (!config::HasProfilesLoaded()) {
                return HOCCLK_ERROR(ConfigNotLoaded);
            }
            // HOC indices 0-4 = PollingIntervalMs..CsvWriteIntervalMs, same as sys-clk
            for (int i = 0; i < SYSCLK_CONFIG_ENUMMAX; i++) {
                out_configValues->values[i] = config::GetConfigValue((HocClkConfigValue)i);
            }
            return 0;
        }

        Result SetConfigValuesHandler(SysClkConfigValueList_Wire* configValues)
        {
            if (!config::HasProfilesLoaded()) {
                return HOCCLK_ERROR(ConfigNotLoaded);
            }
            // Force a full config reload from disk before merging.
            //
            // The overlay writes allow_governing directly to config.ini (not through
            // IPC).  If SetConfigValues arrives within the 2-second FAT mtime window,
            // the in-memory cache still shows allow_governing=0.  Without this call,
            // GetConfigValues() returns the stale 0, and the subsequent SetConfigValues
            // call omits allow_governing from the [values] section — erasing the user's
            // change.  ForceRefresh() guarantees the cache reflects the current file.
            config::ForceRefresh();
            // Read the current full config from in-memory, overwrite only the
            // wire-exposed values (indices 0..SYSCLK_CONFIG_ENUMMAX-1), then
            // persist the whole struct in one call.
            //
            // BUG that was here:
            //   SetConfigValue(i, newVal, /*immediate=*/false) writes each key
            //   to the INI file but deliberately SKIPS updating configValues[i]
            //   in memory.  GetConfigValues() then reads the still-stale
            //   in-memory array, and the subsequent SetConfigValues() call
            //   overwrites the INI with those stale values — silently discarding
            //   everything the loop just wrote.
            HocClkConfigValueList full;
            config::GetConfigValues(&full);
            for (int i = 0; i < SYSCLK_CONFIG_ENUMMAX; i++) {
                full.values[i] = configValues->values[i];
            }
            if (!config::SetConfigValues(&full, true)) {
                return HOCCLK_ERROR(ConfigSaveFailed);
            }
            return 0;
        }

        Result GetFreqList(SysClkIpc_GetFreqList_Args_Wire* args,
                           std::uint32_t* out_list,
                           std::size_t size,
                           std::uint32_t* out_count)
        {
            if (args->module >= SYSCLK_MODULE_ENUMMAX) {
                return HOCCLK_ERROR(Generic);
            }
            if (args->maxCount != size / sizeof(*out_list)) {
                return HOCCLK_ERROR(Generic);
            }
            clockManager::GetFreqList((HocClkModule)args->module, out_list, args->maxCount, out_count);
            return 0;
        }

        Result ServiceHandlerFunc(void* arg, const IpcServerRequest* r, u8* out_data, size_t* out_dataSize)
        {
            (void)arg;
            switch (r->data.cmdId)
            {
                // -----------------------------------------------------------
                // Cmd 0: GetApiVersion
                // serviceDispatchOut → raw out u32
                // -----------------------------------------------------------
                case SysClkIpcCmd_GetApiVersion:
                    *out_dataSize = sizeof(u32);
                    return GetApiVersion((u32*)out_data);

                // -----------------------------------------------------------
                // Cmd 1: GetVersionString
                // serviceDispatch with SfBufferAttr_HipcMapAlias|Out → recv buffer
                // (overlay really does send a buffer here — correct as-is)
                // -----------------------------------------------------------
                case SysClkIpcCmd_GetVersionString:
                    if (r->hipc.meta.num_recv_buffers >= 1) {
                        return GetVersionString(
                            (char*)hipcGetBufferAddress(r->hipc.data.recv_buffers),
                            hipcGetBufferSize(r->hipc.data.recv_buffers)
                        );
                    }
                    break;

                // -----------------------------------------------------------
                // Cmd 2: GetCurrentContext
                // serviceDispatchOut → raw out SysClkContext (104 bytes)
                // FIXED: was checking num_recv_buffers; overlay sends NO buffer.
                // -----------------------------------------------------------
                case SysClkIpcCmd_GetCurrentContext:
                    *out_dataSize = sizeof(SysClkContext_Wire);
                    return GetCurrentContext((SysClkContext_Wire*)out_data);

                // -----------------------------------------------------------
                // Cmd 3: Exit
                // -----------------------------------------------------------
                case SysClkIpcCmd_Exit:
                    return ExitHandler();

                // -----------------------------------------------------------
                // Cmd 4: GetProfileCount
                // serviceDispatchInOut → raw in u64, raw out u8
                // -----------------------------------------------------------
                case SysClkIpcCmd_GetProfileCount:
                    if (r->data.size >= sizeof(std::uint64_t)) {
                        *out_dataSize = sizeof(std::uint8_t);
                        return GetProfileCount((std::uint64_t*)r->data.ptr, (std::uint8_t*)out_data);
                    }
                    break;

                // -----------------------------------------------------------
                // Cmd 5: GetProfiles
                // serviceDispatchInOut → raw in u64, raw out SysClkTitleProfileList (60 bytes)
                // FIXED: was checking num_recv_buffers; overlay sends NO buffer.
                // -----------------------------------------------------------
                case SysClkIpcCmd_GetProfiles:
                    if (r->data.size >= sizeof(std::uint64_t)) {
                        *out_dataSize = sizeof(SysClkTitleProfileList_Wire);
                        return GetProfiles((std::uint64_t*)r->data.ptr,
                                           (SysClkTitleProfileList_Wire*)out_data);
                    }
                    break;

                // -----------------------------------------------------------
                // Cmd 6: SetProfiles
                // serviceDispatchIn → raw in SysClkIpc_SetProfiles_Args (68 bytes)
                // -----------------------------------------------------------
                case SysClkIpcCmd_SetProfiles:
                    if (r->data.size >= sizeof(SysClkIpc_SetProfiles_Args_Wire)) {
                        return SetProfiles((SysClkIpc_SetProfiles_Args_Wire*)r->data.ptr);
                    }
                    break;

                // -----------------------------------------------------------
                // Cmd 7: SetEnabled
                // serviceDispatchIn → raw in u8
                // -----------------------------------------------------------
                case SysClkIpcCmd_SetEnabled:
                    if (r->data.size >= sizeof(std::uint8_t)) {
                        return SetEnabled((std::uint8_t*)r->data.ptr);
                    }
                    break;

                // -----------------------------------------------------------
                // Cmd 8: SetOverride
                // serviceDispatchIn → raw in SysClkIpc_SetOverride_Args (8 bytes)
                // -----------------------------------------------------------
                case SysClkIpcCmd_SetOverride:
                    if (r->data.size >= sizeof(SysClkIpc_SetOverride_Args_Wire)) {
                        return SetOverride((SysClkIpc_SetOverride_Args_Wire*)r->data.ptr);
                    }
                    break;

                // -----------------------------------------------------------
                // Cmd 9: GetConfigValues
                // serviceDispatchOut → raw out SysClkConfigValueList (40 bytes)
                // FIXED: was checking num_recv_buffers; overlay sends NO buffer.
                // -----------------------------------------------------------
                case SysClkIpcCmd_GetConfigValues:
                    *out_dataSize = sizeof(SysClkConfigValueList_Wire);
                    return GetConfigValuesHandler((SysClkConfigValueList_Wire*)out_data);

                // -----------------------------------------------------------
                // Cmd 10: SetConfigValues
                // serviceDispatchIn → raw in SysClkConfigValueList (40 bytes)
                // FIXED: was checking num_send_buffers; overlay sends raw data only.
                // -----------------------------------------------------------
                case SysClkIpcCmd_SetConfigValues:
                    if (r->data.size >= sizeof(SysClkConfigValueList_Wire)) {
                        return SetConfigValuesHandler((SysClkConfigValueList_Wire*)r->data.ptr);
                    }
                    break;

                // -----------------------------------------------------------
                // Cmd 11: GetFreqList
                // serviceDispatchInOut + SfBufferAttr_HipcAutoSelect|Out
                //   raw in:  SysClkIpc_GetFreqList_Args (8 bytes)
                //   raw out: u32 outCount
                //   hipc recv buffer: u32[] freq list
                // (overlay really does send a buffer — correct as-is)
                // -----------------------------------------------------------
                case SysClkIpcCmd_GetFreqList:
                    if (r->data.size >= sizeof(SysClkIpc_GetFreqList_Args_Wire) &&
                        r->hipc.meta.num_recv_buffers >= 1)
                    {
                        *out_dataSize = sizeof(std::uint32_t);
                        return GetFreqList(
                            (SysClkIpc_GetFreqList_Args_Wire*)r->data.ptr,
                            (std::uint32_t*)hipcGetBufferAddress(r->hipc.data.recv_buffers),
                            hipcGetBufferSize(r->hipc.data.recv_buffers),
                            (std::uint32_t*)out_data
                        );
                    }
                    break;
            }

            return HOCCLK_ERROR(Generic);
        }

        void ProcessThreadFunc(void* arg)
        {
            (void)arg;
            Result rc;
            while (true) {
                rc = ipcServerProcess(&gServer, &ServiceHandlerFunc, nullptr);
                if (R_FAILED(rc)) {
                    if (rc == KERNELRESULT(Cancelled)) {
                        return;
                    }
                    if (rc != KERNELRESULT(ConnectionClosed)) {
                        fileUtils::LogLine("[ipc] ipcServerProcess: [0x%x] %04d-%04d",
                                           rc, R_MODULE(rc), R_DESCRIPTION(rc));
                    }
                }
            }
        }

    } // anonymous namespace

    void Initialize()
    {
        std::int32_t priority;
        Result rc = svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
        ASSERT_RESULT_OK(rc, "svcGetThreadPriority");
        rc = ipcServerInit(&gServer, SYSCLK_IPC_SERVICE_NAME, 42);
        ASSERT_RESULT_OK(rc, "ipcServerInit");
        rc = threadCreate(&gThread, &ProcessThreadFunc, nullptr, NULL, 0x4000, priority, -2);
        ASSERT_RESULT_OK(rc, "threadCreate");
        gRunning = false;
    }

    void Exit()
    {
        SetRunning(false);
        Result rc = threadClose(&gThread);
        ASSERT_RESULT_OK(rc, "threadClose");
        rc = ipcServerExit(&gServer);
        ASSERT_RESULT_OK(rc, "ipcServerExit");
    }

    void SetRunning(bool running)
    {
        std::scoped_lock lock{gThreadMutex};
        if (gRunning == running) {
            return;
        }

        gRunning = running;

        if (running) {
            Result rc = threadStart(&gThread);
            ASSERT_RESULT_OK(rc, "threadStart");
        } else {
            svcCancelSynchronization(gThread.handle);
            threadWaitForExit(&gThread);
        }
    }

} // namespace ipcService
