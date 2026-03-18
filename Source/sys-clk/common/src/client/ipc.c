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


#define NX_SERVICE_ASSUME_NON_DOMAIN
#include <switch.h>
#include <string.h>
#include <stdatomic.h>
#include <sysclk/client/ipc.h>

static Service g_sysclkSrv;
static atomic_size_t g_refCnt;

bool sysclkIpcRunning()
{
    Handle handle;
    bool running = R_FAILED(smRegisterService(&handle, smEncodeName(SYSCLK_IPC_SERVICE_NAME), false, 1));

    if (!running)
    {
        smUnregisterService(smEncodeName(SYSCLK_IPC_SERVICE_NAME));
    }

  return running;
}

Result sysclkIpcInitialize(void)
{
    Result rc = 0;

    g_refCnt++;

    if (serviceIsActive(&g_sysclkSrv))
        return 0;

    rc = smGetService(&g_sysclkSrv, SYSCLK_IPC_SERVICE_NAME);

    if (R_FAILED(rc)) sysclkIpcExit();

    return rc;
}

void sysclkIpcExit(void)
{
    if (--g_refCnt == 0)
    {
        serviceClose(&g_sysclkSrv);
    }
}

Result sysclkIpcGetAPIVersion(u32* out_ver)
{
    return serviceDispatchOut(&g_sysclkSrv, SysClkIpcCmd_GetApiVersion, *out_ver);
}

Result sysclkIpcGetVersionString(char* out, size_t len)
{
    return serviceDispatch(&g_sysclkSrv, SysClkIpcCmd_GetVersionString,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
        .buffers = {{out, len}},
    );
}

Result sysclkIpcGetCurrentContext(SysClkContext* out_context)
{
    return serviceDispatch(&g_sysclkSrv, SysClkIpcCmd_GetCurrentContext,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
        .buffers = {{out_context, sizeof(SysClkContext)}},
    );
}

Result sysclkIpcGetProfileCount(u64 tid, u8* out_count)
{
    return serviceDispatchInOut(&g_sysclkSrv, SysClkIpcCmd_GetProfileCount, tid, *out_count);
}

Result sysclkIpcSetEnabled(bool enabled)
{
    u8 enabledRaw = (u8)enabled;
    return serviceDispatchIn(&g_sysclkSrv, SysClkIpcCmd_SetEnabled, enabledRaw);
}

Result sysclkIpcSetOverride(SysClkModule module, u32 hz)
{
    SysClkIpc_SetOverride_Args args = {
        .module = module,
        .hz = hz
    };
    return serviceDispatchIn(&g_sysclkSrv, SysClkIpcCmd_SetOverride, args);
}

Result sysclkIpcGetProfiles(u64 tid, SysClkTitleProfileList* out_profiles)
{
    return serviceDispatchIn(&g_sysclkSrv, SysClkIpcCmd_GetProfiles, tid,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
        .buffers = {{out_profiles, sizeof(SysClkTitleProfileList)}},
    );
}

Result sysclkIpcSetProfiles(u64 tid, SysClkTitleProfileList* profiles)
{
    SysClkIpc_SetProfiles_Args args;
    args.tid = tid;
    memcpy(&args.profiles, profiles, sizeof(SysClkTitleProfileList));
    return serviceDispatchIn(&g_sysclkSrv, SysClkIpcCmd_SetProfiles, args);
}

Result sysclkIpcGetConfigValues(SysClkConfigValueList* out_configValues)
{
    return serviceDispatch(&g_sysclkSrv, SysClkIpcCmd_GetConfigValues,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
        .buffers = {{out_configValues, sizeof(SysClkConfigValueList)}},
    );
}

Result sysclkIpcSetConfigValues(SysClkConfigValueList* configValues)
{
    return serviceDispatch(&g_sysclkSrv, SysClkIpcCmd_SetConfigValues,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In },
        .buffers = {{configValues, sizeof(SysClkConfigValueList)}},
    );
}

Result sysclkIpcGetFreqList(SysClkModule module, u32* list, u32 maxCount, u32* outCount)
{
    SysClkIpc_GetFreqList_Args args = {
        .module = module,
        .maxCount = maxCount
    };
    return serviceDispatchInOut(&g_sysclkSrv, SysClkIpcCmd_GetFreqList, args, *outCount,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
        .buffers = {{list, maxCount * sizeof(u32)}},
    );
}

Result hocClkIpcSetKipData()
{
    u32 temp = 0;
    return serviceDispatchIn(&g_sysclkSrv, HocClkIpcCmd_SetKipData, temp);
}

Result hocClkIpcGetKipData()
{
    u32 temp = 0;
    return serviceDispatchIn(&g_sysclkSrv, HocClkIpcCmd_GetKipData, temp);
}