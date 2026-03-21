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

#include <nxExt.h>
#include "board.h"
#include "errors.h"
#include "rgltr.h"
#include "file_utils.h"
#include <algorithm> // for std::clamp
#include <math.h>
#include <numeric>
#include <battery.h>
#include <pwm.h>
#include <display_refresh_rate.h>
#include <stdio.h>
#include <cstring>
#include <registers.h>
#include <notification.h>
#include <memmem.h>
#include <minIni.h>
#include <sys/stat.h>
#include <fuse.h>

#define MAX(A, B)   std::max(A, B)
#define MIN(A, B)   std::min(A, B)
#define CEIL(A)     std::ceil(A)
#define FLOOR(A)    std::floor(A)
#define ROUND(A)    std::lround(A)

#define HOSSVC_HAS_CLKRST (hosversionAtLeast(8,0,0))
#define HOSSVC_HAS_TC (hosversionAtLeast(5,0,0))
#define NVGPU_GPU_IOCTL_PMU_GET_GPU_LOAD 0x80044715
#define NVSCHED_CTRL_ENABLE 0x00000601
#define NVSCHED_CTRL_DISABLE 0x00000602

constexpr u64 CpuTimeOutNs = 500'000'000;
constexpr double Systemtickfrequency = 19200000.0 * (static_cast<double>(CpuTimeOutNs) / 1'000'000'000.0);
Result nvInitialize_rc;
Result nvCheck = 1;
Result nvCheck_sched = 1;

LEvent threadexit;
Thread gpuLThread;
Thread cpuCore0Thread;
Thread cpuCore1Thread;
Thread cpuCore2Thread;
Thread cpuCore3Thread;
Thread miscThread;
double temp = 0;

PwmChannelSession g_ICon;
Result pwmCheck = 1;
Result pwmDutyCycleCheck = 1;
double Rotation_Duty = 0;
u8 fanLevel;

uint32_t GPU_Load_u = 0, fd = 0, fd2 = 0;
BatteryChargeInfo info;

static SysClkSocType g_socType = SysClkSocType_Erista;
static HorizonOCConsoleType g_consoleType = HorizonOCConsoleType_Iowa;

u64 idletick0 = 0;
u64 idletick1 = 0;
u64 idletick2 = 0;
// u64 idletick3 = 0;

u32 cpu0, cpu1, cpu2, cpu3, cpuAvg;
u16 cpuSpeedo0, cpuSpeedo2, socSpeedo0; // CPU, GPU, SOC
u32 speedoBracket;
u16 cpuIDDQ, gpuIDDQ, socIDDQ;
u16 BwaferX, BwaferY;
u8 g_dramID = 0;
u64 cldvfs, cldvfs_temp;
u32 cachedEristaUvLowTune0 = 0, cachedEristaUvLowTune1 = 0, cachedMarikoUvHighTune0 = 0;

static const u32 ramBrackets[][22] = {
    { 2133, 2200, 2266, 2300, 2366, 2400, 2433, 2466, 2533, 2566, 2600, 2633, 2700, 2733, 2766, 2833, 2866, 2900, 2933, 3033, 3066, 3100, },
    { 2300, 2366, 2433, 2466, 2533, 2566, 2633, 2700, 2733, 2800, 2833, 2900, 2933, 2966, 3033, 3066, 3100, 3133, 3166, 3200, 3233, 3266, },
    { 2433, 2466, 2533, 2600, 2666, 2733, 2766, 2800, 2833, 2866, 2933, 2966, 3033, 3066, 3100, 3133, 3166, 3200, 3233, 3300, 3333, 3366, },
    { 2500, 2533, 2600, 2633, 2666, 2733, 2800, 2866, 2900, 2966, 3033, 3100, 3166, 3200, 3233, 3266, 3300, 3333, 3366, 3400, 3400, 3400, },
};

static const u32 gpuDvfsArray[] = { 590, 600, 610, 620, 630, 640, 650, 660, 670, 680, 690, 700, 710, 720, 730, 740, 750, 760, 770, 780, 790, 800};

u32 dvfsTable[6][32] = {};
u64 dvfsAddress;
u32 ramVmin;
bool isRetro = false;

const char* Board::GetModuleName(SysClkModule module, bool pretty)
{
    ASSERT_ENUM_VALID(SysClkModule, module);
    return sysclkFormatModule(module, pretty);
}

const char* Board::GetProfileName(SysClkProfile profile, bool pretty)
{
    ASSERT_ENUM_VALID(SysClkProfile, profile);
    return sysclkFormatProfile(profile, pretty);
}

const char* Board::GetThermalSensorName(SysClkThermalSensor sensor, bool pretty)
{
    ASSERT_ENUM_VALID(SysClkThermalSensor, sensor);
    return sysclkFormatThermalSensor(sensor, pretty);
}

const char* Board::GetPowerSensorName(SysClkPowerSensor sensor, bool pretty)
{
    ASSERT_ENUM_VALID(SysClkPowerSensor, sensor);
    return sysclkFormatPowerSensor(sensor, pretty);
}

PcvModule Board::GetPcvModule(SysClkModule sysclkModule)
{
    switch(sysclkModule)
    {
        case SysClkModule_CPU:
            return PcvModule_CpuBus;
        case SysClkModule_GPU:
            return PcvModule_GPU;
        case SysClkModule_MEM:
            return PcvModule_EMC;
        default:
            ASSERT_ENUM_VALID(SysClkModule, sysclkModule);
    }

    return (PcvModule)0;
}

PcvModuleId Board::GetPcvModuleId(SysClkModule sysclkModule)
{
    PcvModuleId pcvModuleId;
    Result rc = pcvGetModuleId(&pcvModuleId, GetPcvModule(sysclkModule));
    ASSERT_RESULT_OK(rc, "pcvGetModuleId");

    return pcvModuleId;
}

void CheckCore(void *idletickPtr) {
	u64* idletick = static_cast<u64 *>(idletickPtr);
	while(true) {
		u64 idletickA;
		u64 idletickB;
		svcGetInfo(&idletickB, InfoType_IdleTickCount, INVALID_HANDLE, -1);
		svcWaitForAddress(&threadexit, ArbitrationType_WaitIfEqual, 0, CpuTimeOutNs);
		svcGetInfo(&idletickA, InfoType_IdleTickCount, INVALID_HANDLE, -1);
		*idletick = idletickA - idletickB;
	}
}

void gpuLoadThread(void*) {
    #define gpu_samples_average 8
    uint32_t gpu_load_array[gpu_samples_average] = {0};
    size_t i = 0;
    if (R_SUCCEEDED(nvCheck)) do {
        u32 temp;
        if (R_SUCCEEDED(nvIoctl(fd, NVGPU_GPU_IOCTL_PMU_GET_GPU_LOAD, &temp))) {
            gpu_load_array[i++ % gpu_samples_average] = temp;
            GPU_Load_u = std::accumulate(&gpu_load_array[0], &gpu_load_array[gpu_samples_average], 0) / gpu_samples_average;
        }
        svcSleepThread(16'666'000); // wait a bit (this is the perfect amount of time to keep the reading accurate)
    } while(true);
}

void miscThreadFunc(void*) {
    for(;;) {
        if (R_SUCCEEDED(pwmCheck)) {
            if (R_SUCCEEDED(pwmChannelSessionGetDutyCycle(&g_ICon, &temp))) {
                temp *= 10;
                temp = trunc(temp);
                temp /= 10;
                Rotation_Duty = 100.0 - temp;
            }
        }
        fanLevel = (u8)Rotation_Duty;
        svcSleepThread(300'000'000);
    }
}

void Board::Initialize()
{
    Result rc = 0;
    if(HOSSVC_HAS_CLKRST)
    {
        rc = clkrstInitialize();
        ASSERT_RESULT_OK(rc, "clkrstInitialize");
    }
    else
    {
        rc = pcvInitialize();
        ASSERT_RESULT_OK(rc, "pcvInitialize");
    }

    rc = apmExtInitialize();
    ASSERT_RESULT_OK(rc, "apmExtInitialize");

    rc = psmInitialize();
    ASSERT_RESULT_OK(rc, "psmInitialize");

    if(HOSSVC_HAS_TC)
    {
        rc = tcInitialize();
        ASSERT_RESULT_OK(rc, "tcInitialize");
    }

    rc = max17050Initialize();
    ASSERT_RESULT_OK(rc, "max17050Initialize");

    rc = tmp451Initialize();
    ASSERT_RESULT_OK(rc, "tmp451Initialize");
    nvInitialize_rc = nvInitialize();
    if (R_SUCCEEDED(nvInitialize_rc)) {
        nvCheck = nvOpen(&fd, "/dev/nvhost-ctrl-gpu");
        nvCheck_sched = nvOpen(&fd2, "/dev/nvsched-ctrl");
    }

    rc = rgltrInitialize();
    ASSERT_RESULT_OK(rc, "rgltrInitialize");

    // if (R_SUCCEEDED(fanInitialize())) {
    //     if (hosversionAtLeast(7,0,0)) fanCheck = fanOpenController(&fanController, 0x3D000001);
    //     else fanCheck = fanOpenController(&fanController, 1);
    // }

    rc = pmdmntInitialize();
    ASSERT_RESULT_OK(rc, "pmdmntInitialize");

    threadCreate(&gpuLThread, gpuLoadThread, NULL, NULL, 0x1000, 0x3F, -2);
	threadStart(&gpuLThread);
    leventClear(&threadexit);
    threadCreate(&cpuCore0Thread, CheckCore, &idletick0, NULL, 0x1000, 0x10, 0);
    threadCreate(&cpuCore1Thread, CheckCore, &idletick1, NULL, 0x1000, 0x10, 1);
    threadCreate(&cpuCore2Thread, CheckCore, &idletick2, NULL, 0x1000, 0x10, 2);
    // threadCreate(&cpuCore3Thread, CheckCore, &idletick3, NULL, 0x1000, 0x10, 3);
    threadCreate(&miscThread, miscThreadFunc, NULL, NULL, 0x1000, 0x10, 3);

    threadStart(&cpuCore0Thread);
    threadStart(&cpuCore1Thread);
    threadStart(&cpuCore2Thread);
    // threadStart(&cpuCore3Thread);
    threadStart(&miscThread);
    batteryInfoInitialize();
    FetchHardwareInfos();

    if (hosversionAtLeast(6,0,0) && R_SUCCEEDED(pwmInitialize())) {
        pwmCheck = pwmOpenSession2(&g_ICon, 0x3D000001);
    }

    struct stat st = {0};
    isRetro = (stat("sdmc:/" FILE_CONFIG_DIR "/retro.flag", &st) == 0);

    u64 clkVirtAddr, dsiVirtAddr, outsize;
    rc = svcQueryMemoryMapping(&clkVirtAddr, &outsize, 0x60006000, 0x1000);
    ASSERT_RESULT_OK(rc, "svcQueryMemoryMapping (clk)");
    rc = svcQueryMemoryMapping(&dsiVirtAddr, &outsize, 0x54300000, 0x40000);
    ASSERT_RESULT_OK(rc, "svcQueryMemoryMapping (dsi)");

    DisplayRefreshConfig cfg = {.clkVirtAddr = clkVirtAddr, .dsiVirtAddr = dsiVirtAddr, .isLite = IsHoag(), .isRetroSUPER = isRetro, .isPossiblySpoofedRetro = isRetro};

    DisplayRefresh_Initialize(&cfg);

    rc = svcQueryMemoryMapping(&cldvfs, &cldvfs_temp, CLDVFS_REGION_BASE, CLDVFS_REGION_SIZE);
    ASSERT_RESULT_OK(rc, "svcQueryMemoryMapping (cldvfs)");

    if(Board::GetSocType() == SysClkSocType_Erista) {
        cachedEristaUvLowTune0 = *(u32*)(cldvfs + CL_DVFS_TUNE0_0);
        cachedEristaUvLowTune1 = *(u32*)(cldvfs + CL_DVFS_TUNE1_0);
    } else {
        Board::SetHz(SysClkModule_CPU, 1785000000);
        cachedMarikoUvHighTune0 = *(u32*)(cldvfs + CL_DVFS_TUNE0_0);
        Board::ResetToStockCpu();
    }


}

bool Board::IsUsingRetroSuperDisplay() {
    return isRetro;
}

void Board::GetWaferPosition(u16* x, u16* y) {
    *x = BwaferX;
    *y = BwaferY;
}

void Board::fuseReadSpeedos() {

    u64 pid = 0;
    if (R_FAILED(pmdmntGetProcessId(&pid, 0x0100000000000006))) {
        return;
    }

    Handle debug;
    if (R_FAILED(svcDebugActiveProcess(&debug, pid))) {
        return;
    }

    MemoryInfo mem_info = {0};
    u32 pageinfo = 0;
    u64 addr = 0;

    char stack[0x10] = {0};
    const char compare[0x10] = {0};
    char dump[0x400] = {0};

    while (true) {
        if (R_FAILED(svcQueryDebugProcessMemory(&mem_info, &pageinfo, debug, addr)) || mem_info.addr < addr) {
            break;
        }

        if (mem_info.type == MemType_Io && mem_info.size == 0x1000) {
            if (R_FAILED(svcReadDebugProcessMemory(stack, debug, mem_info.addr, sizeof(stack)))) {
                break;
            }

            if (memcmp(stack, compare, sizeof(stack)) == 0) {
                if (R_FAILED(svcReadDebugProcessMemory(dump, debug, mem_info.addr + 0x800, sizeof(dump)))) {
                    break;
                }

                cpuSpeedo0 = *reinterpret_cast<const u16*>(dump + FUSE_CPU_SPEEDO_0_CALIB);
                cpuSpeedo2 = *reinterpret_cast<const u16*>(dump + FUSE_CPU_SPEEDO_2_CALIB);
                socSpeedo0 = *reinterpret_cast<const u16*>(dump + FUSE_SOC_SPEEDO_0_CALIB);
                cpuIDDQ = *reinterpret_cast<const u16*>(dump + FUSE_CPU_IDDQ_CALIB) * 4;
                gpuIDDQ = *reinterpret_cast<const u16*>(dump + FUSE_GPU_IDDQ_CALIB) * 5;
                socIDDQ = *reinterpret_cast<const u16*>(dump + FUSE_SOC_IDDQ_CALIB) * 4;
                BwaferX = *reinterpret_cast<const u16*>(dump + FUSE_OPT_X_COORDINATE);
                BwaferY = *reinterpret_cast<const u16*>(dump + FUSE_OPT_Y_COORDINATE);

                svcCloseHandle(debug);
                return;
            }
        }

        addr = mem_info.addr + mem_info.size;
    }

    svcCloseHandle(debug);
}

u16 Board::getSpeedo(HorizonOCSpeedo speedoType) {
    switch(speedoType) {
        case HorizonOCSpeedo_CPU:
            return cpuSpeedo0;
        case HorizonOCSpeedo_GPU:
            return cpuSpeedo2;
        case HorizonOCSpeedo_SOC:
            return socSpeedo0;
        default:
            ASSERT_ENUM_VALID(HorizonOCSpeedo, speedoType);
            return 0;
    }
}

u16 Board::getIDDQ(HorizonOCSpeedo speedoType) {
    switch(speedoType) {
        case HorizonOCSpeedo_CPU:
            return cpuIDDQ;
        case HorizonOCSpeedo_GPU:
            return gpuIDDQ;
        case HorizonOCSpeedo_SOC:
            return socIDDQ;
        default:
            ASSERT_ENUM_VALID(HorizonOCSpeedo, speedoType);
            return 0;
    }
}


void Board::Exit()
{
    if(HOSSVC_HAS_CLKRST)
    {
        clkrstExit();
    }
    else
    {
        pcvExit();
    }

    apmExtExit();
    psmExit();

    if(HOSSVC_HAS_TC)
    {
        tcExit();
    }

    max17050Exit();
    tmp451Exit();

    threadClose(&gpuLThread);
    threadClose(&cpuCore0Thread);
    threadClose(&cpuCore1Thread);
    threadClose(&cpuCore2Thread);
    // threadClose(&cpuCore3Thread);
    threadClose(&miscThread);

    DisplayRefresh_Shutdown();

    pwmChannelSessionClose(&g_ICon);
	pwmExit();
    rgltrExit();
    batteryInfoExit();
    pmdmntExit();
    nvExit();

}

SysClkProfile Board::GetProfile()
{
    std::uint32_t mode = 0;
    Result rc = apmExtGetPerformanceMode(&mode);
    ASSERT_RESULT_OK(rc, "apmExtGetPerformanceMode");

    if(mode)
    {
        return SysClkProfile_Docked;
    }

    PsmChargerType chargerType;

    rc = psmGetChargerType(&chargerType);
    ASSERT_RESULT_OK(rc, "psmGetChargerType");

    if(chargerType == PsmChargerType_EnoughPower)
    {
        return SysClkProfile_HandheldChargingOfficial;
    }
    else if(chargerType == PsmChargerType_LowPower)
    {
        return SysClkProfile_HandheldChargingUSB;
    }

    return SysClkProfile_Handheld;
}

void Board::SetHz(SysClkModule module, std::uint32_t hz)
{
    Result rc = 0;
    if(module == HorizonOCModule_Display) {
        DisplayRefresh_SetRate(hz);
        return;
    }
    if(module > SysClkModule_MEM)
        return;
    if(HOSSVC_HAS_CLKRST)
    {
        ClkrstSession session = {0};

        rc = clkrstOpenSession(&session, Board::GetPcvModuleId(module), 3);
        ASSERT_RESULT_OK(rc, "clkrstOpenSession");
        rc = clkrstSetClockRate(&session, hz);
        ASSERT_RESULT_OK(rc, "clkrstSetClockRate");
        if (module == SysClkModule_CPU) {
            svcSleepThread(250'000);
            rc = clkrstSetClockRate(&session, hz);
            ASSERT_RESULT_OK(rc, "clkrstSetClockRate");
        }
        clkrstCloseSession(&session);
    }
    else
    {
        rc = pcvSetClockRate(Board::GetPcvModule(module), hz);
        ASSERT_RESULT_OK(rc, "pcvSetClockRate");
        if (module == SysClkModule_CPU) {
            svcSleepThread(250'000);
            rc = pcvSetClockRate(Board::GetPcvModule(module), hz);
            ASSERT_RESULT_OK(rc, "pcvSetClockRate");
        }
    }
}

std::uint32_t Board::GetHz(SysClkModule module)
{
    Result rc = 0;
    std::uint32_t hz = 0;

    if(module == HorizonOCModule_Display) {
        DisplayRefresh_GetRate(&hz, false);
        return hz;
    }

    if(HOSSVC_HAS_CLKRST)
    {
        ClkrstSession session = {0};

        rc = clkrstOpenSession(&session, Board::GetPcvModuleId(module), 3);
        ASSERT_RESULT_OK(rc, "clkrstOpenSession");

        rc = clkrstGetClockRate(&session, &hz);
        ASSERT_RESULT_OK(rc, "clkrstSetClockRate");

        clkrstCloseSession(&session);
    }
    else
    {
        rc = pcvGetClockRate(Board::GetPcvModule(module), &hz);
        ASSERT_RESULT_OK(rc, "pcvGetClockRate");
    }

    return hz;
}

std::uint32_t Board::GetRealHz(SysClkModule module)
{
    u32 hz = 0;
    switch(module)
    {
        case SysClkModule_CPU:
            return t210ClkCpuFreq();
        case SysClkModule_GPU:
            return t210ClkGpuFreq();
        case SysClkModule_MEM:
            return t210ClkMemFreq();
        case HorizonOCModule_Display:
            DisplayRefresh_GetRate(&hz, false);
            return hz;
        default:
            ASSERT_ENUM_VALID(SysClkModule, module);
    }

    return 0;
}

void Board::GetFreqList(SysClkModule module, std::uint32_t* outList, std::uint32_t maxCount, std::uint32_t* outCount)
{
    Result rc = 0;
    PcvClockRatesListType type;
    s32 tmpInMaxCount = maxCount;
    s32 tmpOutCount = 0;



    if(HOSSVC_HAS_CLKRST)
    {
        ClkrstSession session = {0};

        rc = clkrstOpenSession(&session, Board::GetPcvModuleId(module), 3);
        ASSERT_RESULT_OK(rc, "clkrstOpenSession");

        rc = clkrstGetPossibleClockRates(&session, outList, tmpInMaxCount, &type, &tmpOutCount);
        ASSERT_RESULT_OK(rc, "clkrstGetPossibleClockRates");

        clkrstCloseSession(&session);
    }
    else
    {
        rc = pcvGetPossibleClockRates(Board::GetPcvModule(module), outList, tmpInMaxCount, &type, &tmpOutCount);
        ASSERT_RESULT_OK(rc, "pcvGetPossibleClockRates");
    }

    if(type != PcvClockRatesListType_Discrete)
    {
        ERROR_THROW("Unexpected PcvClockRatesListType: %u (module = %s)", type, Board::GetModuleName(module, false));
    }

    *outCount = tmpOutCount;
}

void Board::ResetToStock()
{
    Result rc = 0;
    if(hosversionAtLeast(9,0,0))
    {
        std::uint32_t confId = 0;
        rc = apmExtGetCurrentPerformanceConfiguration(&confId);
        ASSERT_RESULT_OK(rc, "apmExtGetCurrentPerformanceConfiguration");

        SysClkApmConfiguration* apmConfiguration = NULL;
        for(size_t i = 0; sysclk_g_apm_configurations[i].id; i++)
        {
            if(sysclk_g_apm_configurations[i].id == confId)
            {
                apmConfiguration = &sysclk_g_apm_configurations[i];
                break;
            }
        }

        if(!apmConfiguration)
        {
            ERROR_THROW("Unknown apm configuration: %x", confId);
        }

        Board::SetHz(SysClkModule_CPU, apmConfiguration->cpu_hz);
        Board::SetHz(SysClkModule_GPU, apmConfiguration->gpu_hz);
        Board::SetHz(SysClkModule_MEM, apmConfiguration->mem_hz);
    }
    else
    {
        std::uint32_t mode = 0;
        rc = apmExtGetPerformanceMode(&mode);
        ASSERT_RESULT_OK(rc, "apmExtGetPerformanceMode");

        rc = apmExtSysRequestPerformanceMode(mode);
        ASSERT_RESULT_OK(rc, "apmExtSysRequestPerformanceMode");
    }
}

void Board::ResetToStockCpu()
{
    Result rc = 0;
    if(hosversionAtLeast(9,0,0))
    {
        std::uint32_t confId = 0;
        rc = apmExtGetCurrentPerformanceConfiguration(&confId);
        ASSERT_RESULT_OK(rc, "apmExtGetCurrentPerformanceConfiguration");

        SysClkApmConfiguration* apmConfiguration = NULL;
        for(size_t i = 0; sysclk_g_apm_configurations[i].id; i++)
        {
            if(sysclk_g_apm_configurations[i].id == confId)
            {
                apmConfiguration = &sysclk_g_apm_configurations[i];
                break;
            }
        }

        if(!apmConfiguration)
        {
            ERROR_THROW("Unknown apm configuration: %x", confId);
        }

        Board::SetHz(SysClkModule_CPU, apmConfiguration->cpu_hz);
    }
    else
    {
        std::uint32_t mode = 0;
        rc = apmExtGetPerformanceMode(&mode);
        ASSERT_RESULT_OK(rc, "apmExtGetPerformanceMode");

        rc = apmExtSysRequestPerformanceMode(mode);
        ASSERT_RESULT_OK(rc, "apmExtSysRequestPerformanceMode");
    }
}

void Board::ResetToStockMem()
{
    Result rc = 0;
    if(hosversionAtLeast(9,0,0))
    {
        std::uint32_t confId = 0;
        rc = apmExtGetCurrentPerformanceConfiguration(&confId);
        ASSERT_RESULT_OK(rc, "apmExtGetCurrentPerformanceConfiguration");

        SysClkApmConfiguration* apmConfiguration = NULL;
        for(size_t i = 0; sysclk_g_apm_configurations[i].id; i++)
        {
            if(sysclk_g_apm_configurations[i].id == confId)
            {
                apmConfiguration = &sysclk_g_apm_configurations[i];
                break;
            }
        }

        if(!apmConfiguration)
        {
            ERROR_THROW("Unknown apm configuration: %x", confId);
        }

        Board::SetHz(SysClkModule_MEM, apmConfiguration->mem_hz);
    }
    else
    {
        std::uint32_t mode = 0;
        rc = apmExtGetPerformanceMode(&mode);
        ASSERT_RESULT_OK(rc, "apmExtGetPerformanceMode");

        rc = apmExtSysRequestPerformanceMode(mode);
        ASSERT_RESULT_OK(rc, "apmExtSysRequestPerformanceMode");
    }
}

void Board::ResetToStockGpu()
{
    Result rc = 0;
    if(hosversionAtLeast(9,0,0))
    {
        std::uint32_t confId = 0;
        rc = apmExtGetCurrentPerformanceConfiguration(&confId);
        ASSERT_RESULT_OK(rc, "apmExtGetCurrentPerformanceConfiguration");

        SysClkApmConfiguration* apmConfiguration = NULL;
        for(size_t i = 0; sysclk_g_apm_configurations[i].id; i++)
        {
            if(sysclk_g_apm_configurations[i].id == confId)
            {
                apmConfiguration = &sysclk_g_apm_configurations[i];
                break;
            }
        }

        if(!apmConfiguration)
        {
            ERROR_THROW("Unknown apm configuration: %x", confId);
        }

        Board::SetHz(SysClkModule_GPU, apmConfiguration->gpu_hz);
    }
    else
    {
        std::uint32_t mode = 0;
        rc = apmExtGetPerformanceMode(&mode);
        ASSERT_RESULT_OK(rc, "apmExtGetPerformanceMode");

        rc = apmExtSysRequestPerformanceMode(mode);
        ASSERT_RESULT_OK(rc, "apmExtSysRequestPerformanceMode");
    }
}

void Board::ResetToStockDisplay() {
    DisplayRefresh_SetRate(60);
}

u8 Board::GetHighestDockedDisplayRate() {
    if(Board::GetConsoleType() != HorizonOCConsoleType_Hoag) {
        return DisplayRefresh_GetDockedHighestAllowed();
    } else
        return 60;
}

std::uint32_t Board::GetTemperatureMilli(SysClkThermalSensor sensor)
{
    std::int32_t millis = 0;

    if(sensor == SysClkThermalSensor_SOC)
    {
        millis = tmp451TempSoc();
    }
    else if(sensor == SysClkThermalSensor_PCB)
    {
        millis = tmp451TempPcb();
    }
    else if(sensor == SysClkThermalSensor_Skin)
    {
        if(HOSSVC_HAS_TC)
        {
            Result rc;
            rc = tcGetSkinTemperatureMilliC(&millis);
            ASSERT_RESULT_OK(rc, "tcGetSkinTemperatureMilliC");
        }
    }
    else if (sensor == HorizonOCThermalSensor_Battery) {
        batteryInfoGetChargeInfo(&info);
        millis = batteryInfoGetTemperatureMiliCelsius(&info);
    }
    else if (sensor == HorizonOCThermalSensor_PMIC) {
        millis = 50000;
    }
    else
    {
        ASSERT_ENUM_VALID(SysClkThermalSensor, sensor);
    }

    return std::max(0, millis);
}

std::int32_t Board::GetPowerMw(SysClkPowerSensor sensor)
{
    switch(sensor)
    {
        case SysClkPowerSensor_Now:
            return max17050PowerNow();
        case SysClkPowerSensor_Avg:
            return max17050PowerAvg();
        default:
            ASSERT_ENUM_VALID(SysClkPowerSensor, sensor);
    }

    return 0;
}

u32 GetMaxCpuLoad() {
    float cpuUsage0 = std::clamp(((Systemtickfrequency - idletick0) / static_cast<double>(Systemtickfrequency)) * 1000.0, 0.0, 1000.0);
    float cpuUsage1 = std::clamp(((Systemtickfrequency - idletick1) / static_cast<double>(Systemtickfrequency)) * 1000.0, 0.0, 1000.0);
    float cpuUsage2 = std::clamp(((Systemtickfrequency - idletick2) / static_cast<double>(Systemtickfrequency)) * 1000.0, 0.0, 1000.0);
    // float cpuUsage3 = std::clamp(((Systemtickfrequency - idletick3) / static_cast<double>(Systemtickfrequency)) * 1000.0, 0.0, 1000.0);

    return std::round(std::max({cpuUsage0, cpuUsage1, cpuUsage2}));
}

std::uint32_t Board::GetPartLoad(SysClkPartLoad loadSource)
{
    switch(loadSource)
    {
        case SysClkPartLoad_EMC:
            return t210EmcLoadAll();
        case SysClkPartLoad_EMCCpu:
            return t210EmcLoadCpu();
        case HocClkPartLoad_GPU:
            return GPU_Load_u;
        case HocClkPartLoad_CPUMax:
            return GetMaxCpuLoad();
        case HocClkPartLoad_BAT:
            batteryInfoGetChargeInfo(&info);
            return info.RawBatteryCharge;
        case HocClkPartLoad_FAN:
            return fanLevel;
        default:
            ASSERT_ENUM_VALID(SysClkPartLoad, loadSource);
    }

    return 0;
}


SysClkSocType Board::GetSocType() {
    return g_socType;
}

HorizonOCConsoleType Board::GetConsoleType() {
    return g_consoleType;
}

u8 Board::GetDramID() {
    return g_dramID;
}

void Board::FetchHardwareInfos()
{
    fuseReadSpeedos();
    SetSpeedoBracket();
    u64 sku = 0, dramID = 0;
    Result rc = splInitialize();
    ASSERT_RESULT_OK(rc, "splInitialize");

    rc = splGetConfig(SplConfigItem_HardwareType, &sku);
    ASSERT_RESULT_OK(rc, "splGetConfig");

    rc = splGetConfig(SplConfigItem_DramId, &dramID);
    ASSERT_RESULT_OK(rc, "splGetConfig");

    splExit();

    switch(sku)
    {
        case 2:
        case 3:
        case 4:
        case 5:
            g_socType = SysClkSocType_Mariko;
            break;
        default:
            g_socType = SysClkSocType_Erista;
    }

    if (g_socType == SysClkSocType_Mariko) {
        CacheDvfsTable();
    }

    g_consoleType = (HorizonOCConsoleType)sku;
    g_dramID = (u8)dramID;
}

/*
* Switch Power domains (max77620):
* Name  | Usage         | uV step | uV min | uV default | uV max  | Init
*-------+---------------+---------+--------+------------+---------+------------------
*  sd0  | SoC           | 12500   | 600000 |  625000    | 1400000 | 1.125V (pkg1.1)
*  sd1  | SDRAM         | 12500   | 600000 | 1125000    | 1125000 | 1.1V   (pkg1.1)
*  sd2  | ldo{0-1, 7-8} | 12500   | 600000 | 1325000    | 1350000 | 1.325V (pcv)
*  sd3  | 1.8V general  | 12500   | 600000 | 1800000    | 1800000 |
*  ldo0 | Display Panel | 25000   | 800000 | 1200000    | 1200000 | 1.2V   (pkg1.1)
*  ldo1 | XUSB, PCIE    | 25000   | 800000 | 1050000    | 1050000 | 1.05V  (pcv)
*  ldo2 | SDMMC1        | 50000   | 800000 | 1800000    | 3300000 |
*  ldo3 | GC ASIC       | 50000   | 800000 | 3100000    | 3100000 | 3.1V   (pcv)
*  ldo4 | RTC           | 12500   | 800000 |  850000    |  850000 | 0.85V  (AO, pcv)
*  ldo5 | GC Card       | 50000   | 800000 | 1800000    | 1800000 | 1.8V   (pcv)
*  ldo6 | Touch, ALS    | 50000   | 800000 | 2900000    | 2900000 | 2.9V   (pcv)
*  ldo7 | XUSB          | 50000   | 800000 | 1050000    | 1050000 | 1.05V  (pcv)
*  ldo8 | XUSB, DP, MCU | 50000   | 800000 | 1050000    | 2800000 | 1.05V/2.8V (pcv)

typedef enum {
    PcvPowerDomainId_Max77620_Sd0  = 0x3A000080,
    PcvPowerDomainId_Max77620_Sd1  = 0x3A000081, // vdd2
    PcvPowerDomainId_Max77620_Sd2  = 0x3A000082,
    PcvPowerDomainId_Max77620_Sd3  = 0x3A000083,
    PcvPowerDomainId_Max77620_Ldo0 = 0x3A0000A0,
    PcvPowerDomainId_Max77620_Ldo1 = 0x3A0000A1,
    PcvPowerDomainId_Max77620_Ldo2 = 0x3A0000A2,
    PcvPowerDomainId_Max77620_Ldo3 = 0x3A0000A3,
    PcvPowerDomainId_Max77620_Ldo4 = 0x3A0000A4,
    PcvPowerDomainId_Max77620_Ldo5 = 0x3A0000A5,
    PcvPowerDomainId_Max77620_Ldo6 = 0x3A0000A6,
    PcvPowerDomainId_Max77620_Ldo7 = 0x3A0000A7,
    PcvPowerDomainId_Max77620_Ldo8 = 0x3A0000A8,
    PcvPowerDomainId_Max77621_Cpu  = 0x3A000003,
    PcvPowerDomainId_Max77621_Gpu  = 0x3A000004,
    PcvPowerDomainId_Max77812_Cpu  = 0x3A000003,
    PcvPowerDomainId_Max77812_Gpu  = 0x3A000004,
    PcvPowerDomainId_Max77812_Dram = 0x3A000005, // vddq
} PowerDomainId;

*/

std::uint32_t Board::GetVoltage(HocClkVoltage voltage)
{
    RgltrSession session;
    Result rc = 0;
    u32 out = 0;
    switch(voltage)
    {
        case HocClkVoltage_SOC:
            rc = rgltrOpenSession(&session, PcvPowerDomainId_Max77620_Sd0);
            ASSERT_RESULT_OK(rc, "rgltrOpenSession")
            rgltrGetVoltage(&session, &out);
            rgltrCloseSession(&session);
            break;
        case HocClkVoltage_EMCVDD2:
            rc = rgltrOpenSession(&session, PcvPowerDomainId_Max77620_Sd1);
            ASSERT_RESULT_OK(rc, "rgltrOpenSession")
            rgltrGetVoltage(&session, &out);
            rgltrCloseSession(&session);
            break;
        case HocClkVoltage_CPU:
            if(Board::GetSocType() == SysClkSocType_Mariko)
                rc = rgltrOpenSession(&session, PcvPowerDomainId_Max77621_Cpu);
            else
                rc = rgltrOpenSession(&session, PcvPowerDomainId_Max77812_Cpu);
            ASSERT_RESULT_OK(rc, "rgltrOpenSession")
            rgltrGetVoltage(&session, &out);
            rgltrCloseSession(&session);
            break;
        case HocClkVoltage_GPU:
            if(Board::GetSocType() == SysClkSocType_Mariko)
                rc = rgltrOpenSession(&session, PcvPowerDomainId_Max77621_Gpu);
            else
                rc = rgltrOpenSession(&session, PcvPowerDomainId_Max77812_Gpu);
            ASSERT_RESULT_OK(rc, "rgltrOpenSession")
            rgltrGetVoltage(&session, &out);
            rgltrCloseSession(&session);
            break;
        case HocClkVoltage_EMCVDDQ_MarikoOnly:
            if(Board::GetSocType() == SysClkSocType_Mariko) {
                rc = rgltrOpenSession(&session, PcvPowerDomainId_Max77812_Dram);
                ASSERT_RESULT_OK(rc, "rgltrOpenSession")
                rgltrGetVoltage(&session, &out);
                rgltrCloseSession(&session);
            } else {
                out = Board::GetVoltage(HocClkVoltage_EMCVDD2);
            }
            break;
        case HocClkVoltage_Display:
            rc = rgltrOpenSession(&session, PcvPowerDomainId_Max77620_Ldo0);
            ASSERT_RESULT_OK(rc, "rgltrOpenSession")
            rgltrGetVoltage(&session, &out);
            rgltrCloseSession(&session);
            break;
        case HocClkVoltage_Battery:
            batteryInfoGetChargeInfo(&info);
            out = info.VoltageAvg;
            break;
        default:
            ASSERT_ENUM_VALID(HocClkVoltage, voltage);
    }

    return out > 0 ? out : 0;
}

void Board::SetSpeedoBracket() {
    if (cpuSpeedo2 >= 1754) {
        speedoBracket = 3;
    } else if (cpuSpeedo2 >= 1690) {
        speedoBracket = 2;
    } else if (cpuSpeedo2 > 1625) {
        speedoBracket = 1;
    } else {
        speedoBracket = 0;
    }
}

u32 Board::GetMinimumGpuVoltage(u32 freqMhz) {
    if (freqMhz <= 1600)
        return 0;

    for (u32 voltageIndex = 0; voltageIndex < 22; ++voltageIndex) {
        if (freqMhz <= ramBrackets[speedoBracket][voltageIndex]) {
            return gpuDvfsArray[voltageIndex];
        }
    }

    return 800;
}

Handle Board::GetPcvHandle() {
    constexpr u64 PcvID = 0x10000000000001a;
    u64 processIDList[80]{};
    s32 processCount    = 0;
    Handle handle       = INVALID_HANDLE;

    DebugEventInfo debugEvent{};

    /* Get all running processes. */
    Result resultGetProcessList = svcGetProcessList(&processCount, processIDList, std::size(processIDList));
    if (R_FAILED(resultGetProcessList)) {
        return INVALID_HANDLE;
    }

    /* Try to find pcv. */
    for (int i = 0; i < processCount; ++i) {
        if (handle != INVALID_HANDLE) {
            svcCloseHandle(handle);
            handle = INVALID_HANDLE;
        }

        /* Try to debug process, if it fails, try next process. */
        Result resultSvcDebugProcess = svcDebugActiveProcess(&handle, processIDList[i]);
        if (R_FAILED(resultSvcDebugProcess)) {
            continue;
        }

        /* Try to get a debug event. */
        Result resultDebugEvent = svcGetDebugEvent(&debugEvent, handle);
        if (R_SUCCEEDED(resultDebugEvent)) {
            if (debugEvent.info.create_process.program_id == PcvID) {
                return handle;
            }
        }
    }

    /* Failed to get handle. */
    return INVALID_HANDLE;
}

void Board::CacheDvfsTable() {
    const u32 voltagePattern[] = { 600000, 12500, 1400000, };

    Handle handle = GetPcvHandle();
    if (handle == INVALID_HANDLE) {
        FileUtils::LogLine("[Board] Invalid handle!");
        return;
    }

    MemoryInfo memoryInfo = {};
    u64 address = 0;
    u32 pageInfo = 0;
    constexpr u32 PageSize = 0x1000;
    u8 buffer[PageSize];

    /* Loop until failure. */
    while (true) {
        /* Find pcv heap. */
        while (true) {
            Result resultProcessMemory = svcQueryDebugProcessMemory(&memoryInfo, &pageInfo, handle, address);
            address = memoryInfo.addr + memoryInfo.size;

            if (R_FAILED(resultProcessMemory) || !address) {
                svcCloseHandle(handle);
                FileUtils::LogLine("[Board] Failed to get process data. %u", R_DESCRIPTION(resultProcessMemory));
                handle = INVALID_HANDLE;
                return;
            }

            if (memoryInfo.size && (memoryInfo.perm & 3) == 3 && static_cast<char>(memoryInfo.type) == 0x04) {
                /* Found valid memory. */
                break;
            }
        }

        for (u64 base = 0; base < memoryInfo.size; base += PageSize) {
            u32 memorySize = std::min(memoryInfo.size, static_cast<u64>(PageSize));
            if (R_FAILED(svcReadDebugProcessMemory(buffer, handle, base + memoryInfo.addr, memorySize))) {
                break;
            }

            u8 *resultPattern = static_cast<u8 *>(memmem_impl(buffer, sizeof(buffer), voltagePattern, sizeof(voltagePattern)));
            u32 index = resultPattern - buffer;

            if (!resultPattern) {
                continue;
            }

            /* Assuming mariko. */
            const u32 vmax = 800;
            constexpr u32 DvfsTableOffset = 312;
            if (!std::memcmp(&buffer[index + DvfsTableOffset], &vmax, sizeof(vmax))) {
                std::memcpy(dvfsTable, &buffer[index + DvfsTableOffset], sizeof(dvfsTable));
                dvfsAddress = base + memoryInfo.addr + DvfsTableOffset + index;
            }

            svcCloseHandle(handle);
            handle = INVALID_HANDLE;
            return;
        }
    }

    svcCloseHandle(handle);
    handle = INVALID_HANDLE;
    return;
}

void Board::PcvHijackDvfs(u32 vmin) {
    u32 table[192];
    static_assert(sizeof(table) == sizeof(dvfsTable));
    std::memcpy(table, dvfsTable, sizeof(dvfsTable));

    if (ramVmin == vmin) {
        return;
    }

    for (u32 i = 0; i < std::size(table); ++i) {
        if (table[i] && table[i] <= vmin) {
            table[i] = vmin;
        }
    }

    Handle handle = GetPcvHandle();
    if (handle == INVALID_HANDLE) {
        FileUtils::LogLine("Invalid handle!");
        return;
    }

    Result rc = svcWriteDebugProcessMemory(handle, table, dvfsAddress, sizeof(table));

    if (R_SUCCEEDED(rc)) {
        ramVmin = vmin;
    }

    svcCloseHandle(handle);
    FileUtils::LogLine("[dvfs] voltage set to %u mV", vmin);
}

bool Board::IsDram8GB() {
    SecmonArgs args = {};
    args.X[0] = 0xF0000002;
    args.X[1] = MC_REGISTER_BASE + MC_EMEM_CFG_0;
    svcCallSecureMonitor(&args);

    if(args.X[1] == (MC_REGISTER_BASE + MC_EMEM_CFG_0)) { // if param 1 is identical read failed
        writeNotification("Horizon OC\nSecmon read failed!\n This may be a hardware issue!");
        return false;
    }  else
        return args.X[1] == 0x00002000 ? true : false;
}

void Board::SetGpuSchedulingMode(GpuSchedulingMode mode, GpuSchedulingOverrideMethod method) {
    if (nvCheck_sched == 1 && method == GpuSchedulingOverrideMethod_NvService) {
        return;
    }
    u32 temp;
    bool enabled = false;
    switch(mode) {
        case GpuSchedulingMode_DoNotOverride: break;
        case GpuSchedulingMode_Disabled:
            if(method == GpuSchedulingOverrideMethod_NvService)
                nvIoctl(fd2, NVSCHED_CTRL_DISABLE, &temp);
            else
                enabled = false;
            break;
        case GpuSchedulingMode_Enabled:
            if(method == GpuSchedulingOverrideMethod_NvService)
                nvIoctl(fd2, NVSCHED_CTRL_ENABLE, &temp);
            else
                enabled = true;
            break;
        default:
            ASSERT_ENUM_VALID(GpuSchedulingMode, mode);
    }
    if(method == GpuSchedulingOverrideMethod_Ini) {
        const char* ini_path = "sdmc:/atmosphere/config/system_settings.ini";
        const char* section = "am.gpu";
        const char* key = "gpu_scheduling_enabled";

        const char* value = enabled ? "u8!0x1" : "u8!0x0";

        ini_puts(section, key, value, ini_path);
    }
}
void Board::SetDisplayRefreshDockedState(bool docked) {
    if(Board::GetConsoleType() != HorizonOCConsoleType_Hoag) {
        DisplayRefresh_SetDockedState(docked);
    }
}


typedef struct EristaCpuUvEntry {
    u32 tune0;
    u32 tune1;
} EristaCpuUvEntry;
typedef struct MarikoCpuUvEntry {
    u32 tune0_low;
    u32 tune0_high;
    u32 tune1_low;
    u32 tune1_high;
} MarikoCpuUvEntry;

EristaCpuUvEntry eristaCpuUvTable[5] = {
    {0xffff, 0x27007ff},
    {0xefff, 0x27407ff},
    {0xdfff, 0x27807ff},
    {0xdfdf, 0x27a07ff},
    {0xcfdf, 0x37007ff},
};

MarikoCpuUvEntry marikoCpuUvLow[12] = {
    {0xffa0, 0xffff, 0x21107ff, 0},
    {0x0, 0xffdf, 0x21107ff, 0x27207ff},
    {0xffdf, 0xffdf, 0x21107ff, 0x27307ff},
    {0xffff, 0xffdf, 0x21107ff, 0x27407ff},
    {0x0, 0xffdf, 0x21607ff, 0x27707ff},
    {0x0, 0xffdf, 0x21607ff, 0x27807ff},
    {0x0, 0xdfff, 0x21607ff, 0x27b07ff},
    {0xdfff, 0xdfff, 0x21707ff, 0x27b07ff},
    {0xdfff, 0xdfff, 0x21707ff, 0x27c07ff},
    {0xdfff, 0xdfff, 0x21707ff, 0x27d07ff},
    {0xdfff, 0xdfff, 0x21707ff, 0x27e07ff},
    {0xdfff, 0xdfff, 0x21707ff, 0x27f07ff},
};

MarikoCpuUvEntry marikoCpuUvHigh[12] = {
    {0x0, 0xffff, 0, 0},
    {0x0, 0xffdf, 0, 0x27207ff},
    {0x0, 0xffdf, 0, 0x27307ff},
    {0x0, 0xffdf, 0, 0x27407ff},
    {0x0, 0xffdf, 0, 0x27707ff},
    {0x0, 0xffdf, 0, 0x27807ff},
    {0x0, 0xdfff, 0, 0x27b07ff},
    {0x0, 0xdfff, 0, 0x27c07ff},
    {0x0, 0xdfff, 0, 0x27d07ff},
    {0x0, 0xdfff, 0, 0x27e07ff},
    {0x0, 0xdfff, 0, 0x27f07ff},
    {0x0, 0xdfff, 0, 0x27f07ff},
};
void Board::SetCpuUvLevel(u32 levelLow, u32 levelHigh, u32 tbreakPoint) {
    u32* tune0_ptr = (u32*)(cldvfs + CL_DVFS_TUNE0_0);
    u32* tune1_ptr = (u32*)(cldvfs + CL_DVFS_TUNE1_0);
    if(Board::GetSocType() == SysClkSocType_Mariko) {
        if(Board::GetHz(SysClkModule_CPU) < tbreakPoint && (levelLow || levelHigh)) {
            if(levelLow) {
                *tune0_ptr = marikoCpuUvLow[levelLow-1].tune0_low;
                *tune1_ptr = marikoCpuUvLow[levelLow-1].tune1_low;
            }
            return;
        } else {
            if(levelLow) {
                *tune0_ptr = marikoCpuUvLow[levelLow-1].tune0_low;
                *tune1_ptr = marikoCpuUvLow[levelLow-1].tune1_low;
            }
            if(levelHigh) {
                *tune0_ptr = marikoCpuUvHigh[levelHigh-1].tune0_high;
                *tune1_ptr = marikoCpuUvHigh[levelHigh-1].tune1_high;
            }
            return;
        }
        if(Board::GetHz(SysClkModule_CPU) < tbreakPoint || (!levelLow)) { // account for tbreak
            *tune0_ptr = 0xCFFF;
            *tune1_ptr = 0xFF072201;
            return;
        } else if (Board::GetHz(SysClkModule_CPU) >= tbreakPoint || (!levelHigh)) {
            *tune0_ptr = cachedMarikoUvHighTune0; // per console?
            *tune1_ptr = 0xFFF7FF3F;
            return;
        }
    } else {
        if(Board::GetHz(SysClkModule_CPU) < tbreakPoint || (!levelLow)) { // account for tbreak
            *tune0_ptr = cachedEristaUvLowTune0; // I think each erista has a different tune0/tune1?
            *tune1_ptr = cachedEristaUvLowTune1;
            return;
        } else {
            if(levelLow) {
                *tune0_ptr = eristaCpuUvTable[levelLow-1].tune0;
                *tune1_ptr = eristaCpuUvTable[levelLow-1].tune1;
            } else {
                *tune0_ptr = 0x0;
                *tune1_ptr = 0x0;
            }
        }
    }
}
/*
enum TableConfig: u32 {
    DEFAULT_TABLE = 1,
    TBREAK_1581 = 2,
    TBREAK_1683 = 3,
    EXTREME_TABLE = 4,
};
*/
u32 Board::CalculateTbreak(u32 table) {
    if(Board::GetSocType() == SysClkSocType_Erista)
        return 1581000000;
    else {
        switch(table) {
            case 1 ... 2:
            case 4:
                return 1581000000;
            case 3:
                return 1683000000;
            default:
                return 1581000000;
        }
    }

}

bool Board::IsHoag() {
    return Board::GetConsoleType() == HorizonOCConsoleType_Hoag;
}