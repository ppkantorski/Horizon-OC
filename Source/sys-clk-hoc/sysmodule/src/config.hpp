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

#include <hocclk.h>
#include <switch.h>

#define CONFIG_VAL_SECTION "values"
#define CONFIG_KIP_SECTION "system"

namespace config {

    void Initialize();
    void Exit();

    bool Refresh();
    // Force a full config reload from disk regardless of FAT mtime. Use when the
    // overlay has written keys directly to config.ini without going through IPC,
    // so the 2-second mtime resolution would otherwise leave the cache stale.
    void ForceRefresh();
    // Read dvfs_offset directly from the INI on every tick, bypassing the FAT
    // 2-second mtime resolution.  Returns true (and updates the cached value)
    // when the file value differs from what is currently in memory — i.e. when
    // the overlay has written a new value that Refresh() would otherwise miss.
    bool PollDvfsOffset();
    // Returns true (and clears the flag) if any config value was written via IPC
    // since the last call.  Used by Tick() to trigger SetClocks() immediately
    // on IPC-driven changes without waiting for the FAT mtime to advance.
    bool ConsumeConfigDirty();
    // Signal the clock manager to call SetClocks() on the next tick without
    // waiting for FAT mtime to advance. Called after SetProfiles and
    // SetConfigValues so resets/changes take effect immediately even when
    // multiple writes land within the same 2-second FAT mtime window.
    void MarkConfigDirty();
    bool HasProfilesLoaded();

    // Targeted INI read for allow_governing from the [values] section.
    // Updates the in-memory configValues[] entry when the file has a newer
    // value — avoids a full ForceRefresh() when SetConfigValues is called
    // before Tick has had time to pick up a direct overlay write.
    void SyncAllowGoverningFromFile();

    std::uint8_t GetProfileCount(std::uint64_t tid);
    void GetProfiles(std::uint64_t tid, HocClkTitleProfileList* out_profiles);
    bool SetProfiles(std::uint64_t tid, HocClkTitleProfileList* profiles, bool immediate);
    // Set governor packed values for all profiles for a given TID.
    // Writes to gProfileMHzMap (immediate) and rewrites the TID section in config.ini.
    void SetProfileGovernors(std::uint64_t tid, const std::uint32_t* packed5);
    std::uint32_t GetAutoClockHz(std::uint64_t tid, HocClkModule module, HocClkProfile profile, bool returnRaw);

    void SetEnabled(bool enabled);
    bool Enabled();
    void SetOverrideHz(HocClkModule module, std::uint32_t hz);
    std::uint32_t GetOverrideHz(HocClkModule module);

    std::uint64_t GetConfigValue(HocClkConfigValue val);
    const char* GetConfigValueName(HocClkConfigValue val, bool pretty);
    void GetConfigValues(HocClkConfigValueList* out_configValues);
    bool SetConfigValues(HocClkConfigValueList* configValues, bool immediate);
    bool ResetConfigValue(HocClkConfigValue kval);
    bool SetConfigValue(HocClkConfigValue kval, std::uint64_t value, bool immediate = true);

    extern uint64_t configValues[HocClkConfigValue_EnumMax];

}
