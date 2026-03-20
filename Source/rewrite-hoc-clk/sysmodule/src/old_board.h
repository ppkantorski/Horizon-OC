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
#include <cstdint>
#include <switch.h>
#include <sysclk.h>

class Board
{
  public:
    static void PcvHijackDvfs(u32 vmin);
    static u32 GetMinimumGpuVoltage(u32 freqMhz);
    static void fuseReadSpeedos();
    static u16 getSpeedo(HorizonOCSpeedo speedoType);
    static u16 getIDDQ(HorizonOCSpeedo speedoType);
    static const char* GetProfileName(SysClkProfile profile, bool pretty);
    static const char* GetModuleName(SysClkModule module, bool pretty);
    static const char* GetThermalSensorName(SysClkThermalSensor sensor, bool pretty);
    static const char* GetPowerSensorName(SysClkPowerSensor sensor, bool pretty);
    static void Initialize();
    static void Exit();
    static void ResetToStock();
    static void ResetToStockCpu();
    static void ResetToStockMem();
    static void ResetToStockGpu();
    static void ResetToStockDisplay();
    static u8 GetHighestDockedDisplayRate();
    static SysClkProfile GetProfile();
    static void SetHz(SysClkModule module, std::uint32_t hz);
    static std::uint32_t GetHz(SysClkModule module);
    static std::uint32_t GetRealHz(SysClkModule module);
    static void GetFreqList(SysClkModule module, std::uint32_t* outList, std::uint32_t maxCount, std::uint32_t* outCount);
    static std::uint32_t GetTemperatureMilli(SysClkThermalSensor sensor);
    static std::int32_t GetPowerMw(SysClkPowerSensor sensor);
    static std::uint32_t GetPartLoad(SysClkPartLoad load);
    static SysClkSocType GetSocType();
    static HorizonOCConsoleType GetConsoleType();
    static std::uint32_t GetVoltage(HocClkVoltage voltage);
    static u8 GetFanRotationLevel();
    static u8 GetDramID();
    static bool IsDram8GB();
    static void SetGpuSchedulingMode(GpuSchedulingMode mode, GpuSchedulingOverrideMethod method);
    static void SetDisplayRefreshDockedState(bool docked);
    static void SetCpuUvLevel(u32 levelLow, u32 levelHigh, u32 tbreakPoint);
    static u32 CalculateTbreak(u32 table);
    static bool IsHoag();
  protected:
    static void FetchHardwareInfos();
    static PcvModule GetPcvModule(SysClkModule sysclkModule);
    static PcvModuleId GetPcvModuleId(SysClkModule sysclkModule);
  private:
    static void SetSpeedoBracket();
    static void CacheDvfsTable();
    static Handle GetPcvHandle();
};
