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

#include "config.hpp"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <atomic>
#include <initializer_list>
#include <minIni.h>
#include <nxExt.h>
#include "board/board.hpp"
#include "errors.hpp"
#include "file_utils.hpp"
#include <climits>

namespace config {

    uint64_t configValues[HocClkConfigValue_EnumMax];

    namespace {

        bool gLoaded = false;
        std::string gPath;
        time_t gMtime = 0;
        std::atomic_bool gEnabled{false};
        // Set when any config value is written in-memory via IPC (SetConfigValues /
        // SetConfigValue with immediate=true).  Consumed by Tick() to call SetClocks()
        // right away, bypassing the FAT mtime detection which has 2-second resolution.
        std::atomic_bool gConfigDirty{false};
        std::uint32_t gOverrideFreqs[HocClkModule_EnumMax];
        std::map<std::tuple<std::uint64_t, HocClkProfile, HocClkModule>, std::uint32_t> gProfileMHzMap;
        std::map<std::uint64_t, std::uint8_t> gProfileCountMap;
        LockableMutex gConfigMutex;
        LockableMutex gOverrideMutex;

        time_t CheckModificationTime() {
            time_t mtime = 0;
            struct stat st;
            if (stat(gPath.c_str(), &st) == 0) {
                mtime = st.st_mtime;
            }
            return mtime;
        }

        std::uint32_t FindClockMHz(std::uint64_t tid, HocClkModule module, HocClkProfile profile) {
            if (gLoaded) {
                auto it = gProfileMHzMap.find(std::make_tuple(tid, profile, module));
                if (it != gProfileMHzMap.end()) {
                    return it->second;
                }
            }
            return 0;
        }

        std::uint32_t FindClockHzFromProfiles(std::uint64_t tid, HocClkModule module, std::initializer_list<HocClkProfile> profiles, u32 mhzMultiplier = 1000000) {
            std::uint32_t mhz = 0;

            if (gLoaded) {
                for (auto profile: profiles) {
                    mhz = FindClockMHz(tid, module, profile);
                    if (mhz) {
                        break;
                    }
                }
            }

            return std::max((std::uint32_t)0, mhz * mhzMultiplier);
        }

        int BrowseIniFunc(const char* section, const char* key, const char* value, void* userdata) {
            (void)userdata;
            std::uint64_t input;
            unsigned int kval_start = 0, kval_end = 0;
            if (!strcmp(section, CONFIG_VAL_SECTION)) {
                kval_start = 0;
                kval_end = KipConfigValue_hpMode;
            } else if (!strcmp(section, CONFIG_KIP_SECTION)) {
                kval_start = KipConfigValue_hpMode;
                kval_end = HocClkConfigValue_EnumMax;
            }
            if (kval_end > kval_start) {
                for (unsigned int kval = kval_start; kval < kval_end; kval++) {
                    if (!strcmp(key, hocclkFormatConfigValue((HocClkConfigValue)kval, false))) {
                        // Use strtoll so negative values (e.g. dvfs_offset=-80) are
                        // parsed correctly and survive the implicit cast to s32 at read-time.
                        input = (std::uint64_t)(std::int64_t)strtoll(value, NULL, 0);
                        if (!hocclkValidConfigValue((HocClkConfigValue)kval, input)) {
                            input = hocclkDefaultConfigValue((HocClkConfigValue)kval);
                            fileUtils::LogLine("[cfg] Invalid value for key '%s' in section '%s': using default %d", key, section, input);
                        }
                        configValues[kval] = input;
                        return 1;
                    }
                }
                fileUtils::LogLine("[cfg] Skipping key '%s' in section '%s': Unrecognized config value", key, section);
                return 1;
            }

            std::uint64_t tid = strtoul(section, NULL, 16);

            if (!tid || strlen(section) != 16) {
                fileUtils::LogLine("[cfg] Skipping key '%s' in section '%s': Invalid TitleID", key, section);
                return 1;
            }

            HocClkProfile parsedProfile = HocClkProfile_EnumMax;
            HocClkModule parsedModule = HocClkModule_EnumMax;

            for (unsigned int profile = 0; profile < HocClkProfile_EnumMax; profile++) {
                const char* profileCode = board::GetProfileName((HocClkProfile)profile, false);
                size_t profileCodeLen = strlen(profileCode);

                if (!strncmp(key, profileCode, profileCodeLen) && key[profileCodeLen] == '_') {
                    const char* subkey = key + profileCodeLen + 1;

                    for (unsigned int module = 0; module < HocClkModule_EnumMax; module++) {
                        const char* moduleCode = board::GetModuleName((HocClkModule)module, false);
                        size_t moduleCodeLen = strlen(moduleCode);
                        if (!strncmp(subkey, moduleCode, moduleCodeLen) && subkey[moduleCodeLen] == '\0') {
                            parsedProfile = (HocClkProfile)profile;
                            parsedModule = (HocClkModule)module;
                        }
                    }
                }
            }

            if (parsedModule == HocClkModule_EnumMax || parsedProfile == HocClkProfile_EnumMax) {
                fileUtils::LogLine("[cfg] Skipping key '%s' in section '%s': Unrecognized key", key, section);
                return 1;
            }

            std::uint32_t mhz = strtoul(value, NULL, 10);
            if (!mhz) {
                fileUtils::LogLine("[cfg] Skipping key '%s' in section '%s': Invalid value", key, section);
                return 1;
            }

            gProfileMHzMap[std::make_tuple(tid, parsedProfile, parsedModule)] = mhz;
            auto it = gProfileCountMap.find(tid);
            if (it == gProfileCountMap.end()) {
                gProfileCountMap[tid] = 1;
            } else {
                it->second++;
            }

            return 1;
        }

        void Close() {
            gLoaded = false;
            gProfileMHzMap.clear();
            gProfileCountMap.clear();
            for (unsigned int i = 0; i < HocClkConfigValue_EnumMax; i++) {
                configValues[i] = hocclkDefaultConfigValue((HocClkConfigValue)i);
            }
        }

        void Load() {
            fileUtils::LogLine("[cfg] Reading %s", gPath.c_str());

            Close();
            gMtime = CheckModificationTime();
            if (!gMtime) {
                fileUtils::LogLine("[cfg] Error finding file");
            } else if (!ini_browse(&BrowseIniFunc, nullptr, gPath.c_str())) {
                fileUtils::LogLine("[cfg] Error loading file");
            }

            gLoaded = true;
        }

    }

    void Initialize() {
        gPath = FILE_CONFIG_DIR "/config.ini";
        gLoaded = false;
        gProfileMHzMap.clear();
        gProfileCountMap.clear();
        gMtime = 0;
        gEnabled = false;
        for (unsigned int i = 0; i < HocClkModule_EnumMax; i++) {
            gOverrideFreqs[i] = 0;
        }
        for (unsigned int i = 0; i < HocClkConfigValue_EnumMax; i++) {
            configValues[i] = hocclkDefaultConfigValue((HocClkConfigValue)i);
        }
    }

    void Exit() {
        std::scoped_lock lock{gConfigMutex};
        Close();
    }

    bool Refresh() {
        std::scoped_lock lock{gConfigMutex};
        if (!gLoaded || gMtime != CheckModificationTime()) {
            Load();
            return true;
        }
        return false;
    }

    bool PollDvfsOffset() {
        // Read dvfs_offset directly from the INI on every tick.
        //
        // The overlay writes this key straight to the file without calling IPC,
        // so gConfigDirty is never set and Refresh() misses rapid successive
        // writes because FAT mtime has 2-second resolution.  ini_getl() is a
        // cheap targeted key lookup (no full file scan) that lets us react
        // within one tick (≈300 ms default) regardless of mtime.
        //
        // LONG_MIN is our sentinel for "key absent" — treat as 0 mV (default).
        std::scoped_lock lock{gConfigMutex};
        const char* key = hocclkFormatConfigValue(HocClkConfigValue_DVFSOffset, false);
        long raw = ini_getl(CONFIG_VAL_SECTION, key, LONG_MIN, gPath.c_str());
        int32_t fileMV  = (raw == LONG_MIN) ? 0 : static_cast<int32_t>(raw);
        int32_t cachedMV = static_cast<int32_t>(static_cast<int64_t>(
                               configValues[HocClkConfigValue_DVFSOffset]));
        if (fileMV != cachedMV) {
            configValues[HocClkConfigValue_DVFSOffset] =
                static_cast<uint64_t>(static_cast<int64_t>(fileMV));
            return true;
        }
        return false;
    }

    bool ConsumeConfigDirty() {
        return gConfigDirty.exchange(false);
    }

    bool HasProfilesLoaded() {
        std::scoped_lock lock{gConfigMutex};
        return gLoaded;
    }

    std::uint32_t GetAutoClockHz(std::uint64_t tid, HocClkModule module, HocClkProfile profile, bool returnRaw) {
        std::scoped_lock lock{gConfigMutex};
        switch (profile) {
            case HocClkProfile_Handheld:
                return FindClockHzFromProfiles(tid, module, {HocClkProfile_Handheld}, returnRaw ? 1 : 1000000);
            case HocClkProfile_HandheldCharging:
            case HocClkProfile_HandheldChargingUSB:
                return FindClockHzFromProfiles(tid, module, {HocClkProfile_HandheldChargingUSB, HocClkProfile_HandheldCharging, HocClkProfile_Handheld}, returnRaw ? 1 : 1000000);
            case HocClkProfile_HandheldChargingOfficial:
                return FindClockHzFromProfiles(tid, module, {HocClkProfile_HandheldChargingOfficial, HocClkProfile_HandheldCharging, HocClkProfile_Handheld}, returnRaw ? 1 : 1000000);
            case HocClkProfile_Docked:
                return FindClockHzFromProfiles(tid, module, {HocClkProfile_Docked}, returnRaw ? 1 : 1000000);
            default:
                ERROR_THROW("Unhandled HocClkProfile: %u", profile);
        }
        return 0;
    }

    void GetProfiles(std::uint64_t tid, HocClkTitleProfileList* out_profiles) {
        std::scoped_lock lock{gConfigMutex};
        for (unsigned int profile = 0; profile < HocClkProfile_EnumMax; profile++) {
            for (unsigned int module = 0; module < HocClkModule_EnumMax; module++) {
                out_profiles->mhzMap[profile][module] = FindClockMHz(tid, (HocClkModule)module, (HocClkProfile)profile);
            }
        }
    }

    bool SetProfiles(std::uint64_t tid, HocClkTitleProfileList* profiles, bool immediate) {
        std::scoped_lock lock{gConfigMutex};
        uint8_t numProfiles = 0;

        char section[17] = {0};
        snprintf(section, sizeof(section), "%016lX", tid);

        std::vector<std::string> keys;
        std::vector<std::string> values;
        keys.reserve(+HocClkProfile_EnumMax * +HocClkModule_EnumMax);
        values.reserve(+HocClkProfile_EnumMax * +HocClkModule_EnumMax);

        std::uint32_t* mhz = &profiles->mhz[0];

        for (unsigned int profile = 0; profile < HocClkProfile_EnumMax; profile++) {
            for (unsigned int module = 0; module < HocClkModule_EnumMax; module++) {
                if (*mhz) {
                    numProfiles++;

                    std::string key = std::string(board::GetProfileName((HocClkProfile)profile, false)) +
                                      "_" +
                                      board::GetModuleName((HocClkModule)module, false);
                    std::string value = std::to_string(*mhz);

                    keys.push_back(key);
                    values.push_back(value);
                }
                mhz++;
            }
        }

        std::vector<const char*> keyPointers;
        std::vector<const char*> valuePointers;
        keyPointers.reserve(keys.size() + 1);
        valuePointers.reserve(values.size() + 1);

        for (size_t i = 0; i < keys.size(); i++) {
            keyPointers.push_back(keys[i].c_str());
            valuePointers.push_back(values[i].c_str());
        }
        keyPointers.push_back(NULL);
        valuePointers.push_back(NULL);

        if (!ini_putsection(section, keyPointers.data(), valuePointers.data(), gPath.c_str())) {
            return false;
        }

        if (immediate) {
            mhz = &profiles->mhz[0];
            gProfileCountMap[tid] = numProfiles;
            for (unsigned int profile = 0; profile < HocClkProfile_EnumMax; profile++) {
                for (unsigned int module = 0; module < HocClkModule_EnumMax; module++) {
                    if (*mhz) {
                        gProfileMHzMap[std::make_tuple(tid, (HocClkProfile)profile, (HocClkModule)module)] = *mhz;
                    } else {
                        gProfileMHzMap.erase(std::make_tuple(tid, (HocClkProfile)profile, (HocClkModule)module));
                    }
                    mhz++;
                }
            }
        }

        return true;
    }

    std::uint8_t GetProfileCount(std::uint64_t tid) {
        auto it = gProfileCountMap.find(tid);
        if (it == gProfileCountMap.end()) {
            return 0;
        }
        return it->second;
    }

    void SetEnabled(bool enabled) {
        gEnabled = enabled;
    }

    bool Enabled() {
        return gEnabled;
    }

    void SetOverrideHz(HocClkModule module, std::uint32_t hz) {
        ASSERT_ENUM_VALID(HocClkModule, module);
        std::scoped_lock lock{gOverrideMutex};
        gOverrideFreqs[module] = hz;
    }

    std::uint32_t GetOverrideHz(HocClkModule module) {
        ASSERT_ENUM_VALID(HocClkModule, module);
        std::scoped_lock lock{gOverrideMutex};
        return gOverrideFreqs[module];
    }

    std::uint64_t GetConfigValue(HocClkConfigValue kval) {
        ASSERT_ENUM_VALID(HocClkConfigValue, kval);
        std::scoped_lock lock{gConfigMutex};
        return configValues[kval];
    }

    const char* GetConfigValueName(HocClkConfigValue kval, bool pretty) {
        ASSERT_ENUM_VALID(HocClkConfigValue, kval);
        return hocclkFormatConfigValue(kval, pretty);
    }

    void GetConfigValues(HocClkConfigValueList* out_configValues) {
        std::scoped_lock lock{gConfigMutex};
        for (unsigned int kval = 0; kval < HocClkConfigValue_EnumMax; kval++) {
            out_configValues->values[kval] = configValues[kval];
        }
    }

    bool SetConfigValues(HocClkConfigValueList* configValues, bool immediate) {
        std::scoped_lock lock{gConfigMutex};

        // Write overlay-managed values to [values], kip hardware values to [system]
        auto writeSection = [&](const char* section, unsigned int kvStart, unsigned int kvEnd) -> bool {
            std::vector<const char*> iniKeys;
            std::vector<std::string> iniValues;
            iniKeys.reserve(kvEnd - kvStart + 1);
            iniValues.reserve(kvEnd - kvStart);
            for (unsigned int kval = kvStart; kval < kvEnd; kval++) {
                if (!hocclkValidConfigValue((HocClkConfigValue)kval, configValues->values[kval])) {
                    continue;
                }
                // Always persist DVFSMode so the overlay can always read it back correctly.
                // All other values equal to their default are omitted to keep the ini clean.
                bool isDefault = configValues->values[kval] == hocclkDefaultConfigValue((HocClkConfigValue)kval);
                bool forceWrite = (kval == HocClkConfigValue_DVFSMode);
                if (isDefault && !forceWrite) {
                    continue;
                }
                iniValues.push_back(std::to_string(configValues->values[kval]));
                iniKeys.push_back(hocclkFormatConfigValue((HocClkConfigValue)kval, false));
            }
            iniKeys.push_back(NULL);
            std::vector<const char*> valuePointers;
            valuePointers.reserve(iniValues.size() + 1);
            for (const auto& val : iniValues) {
                valuePointers.push_back(val.c_str());
            }
            valuePointers.push_back(NULL);
            return ini_putsection(section, iniKeys.data(), valuePointers.data(), gPath.c_str()) != 0;
        };

        if (!writeSection(CONFIG_VAL_SECTION, 0, KipConfigValue_hpMode)) {
            return false;
        }
        if (!writeSection(CONFIG_KIP_SECTION, KipConfigValue_hpMode, HocClkConfigValue_EnumMax)) {
            return false;
        }

        if (immediate) {
            for (unsigned int kval = 0; kval < HocClkConfigValue_EnumMax; kval++) {
                if (hocclkValidConfigValue((HocClkConfigValue)kval, configValues->values[kval])) {
                    config::configValues[kval] = configValues->values[kval];
                } else {
                    config::configValues[kval] = hocclkDefaultConfigValue((HocClkConfigValue)kval);
                }
            }
            // Signal Tick() to call SetClocks() on the next tick without waiting
            // for the FAT mtime to advance (2-second resolution on SD cards).
            gConfigDirty.store(true);
        }

        return true;
    }

    bool ResetConfigValue(HocClkConfigValue kval) {
        if (!HOCCLK_ENUM_VALID(HocClkConfigValue, kval)) {
            fileUtils::LogLine("[cfg] Invalid HocClkConfigValue: %u", kval);
            return false;
        }

        std::scoped_lock lock{gConfigMutex};

        std::uint64_t defaultValue = hocclkDefaultConfigValue(kval);

        std::vector<const char*> iniKeys;
        std::vector<std::string> iniValues;
        iniKeys.reserve(2);
        iniValues.reserve(1);

        iniKeys.push_back(hocclkFormatConfigValue(kval, false));
        iniValues.push_back("");
        iniKeys.push_back(NULL);

        std::vector<const char*> valuePointers;
        valuePointers.reserve(iniValues.size() + 1);
        for (const auto& val : iniValues) {
            valuePointers.push_back(val.c_str());
        }
        valuePointers.push_back(NULL);

        const char* section = (kval >= KipConfigValue_hpMode) ? CONFIG_KIP_SECTION : CONFIG_VAL_SECTION;
        if (!ini_putsection(section, iniKeys.data(), valuePointers.data(), gPath.c_str())) {
            fileUtils::LogLine("[cfg] Failed to reset config value %u in INI", kval);
            return false;
        }

        configValues[kval] = defaultValue;
        fileUtils::LogLine("[cfg] Reset config value %u to default: %llu", kval, defaultValue);

        return true;
    }

    bool SetConfigValue(HocClkConfigValue kval, std::uint64_t value, bool immediate) {
        if (!HOCCLK_ENUM_VALID(HocClkConfigValue, kval)) {
            return false;
        }
        if (!hocclkValidConfigValue(kval, value)) {
            return false;
        }

        std::scoped_lock lock{gConfigMutex};

        std::vector<const char*> iniKeys;
        std::vector<std::string> iniValues;
        iniKeys.reserve(2);
        iniValues.reserve(1);

        iniKeys.push_back(hocclkFormatConfigValue(kval, false));
        iniValues.push_back(std::to_string(value));
        iniKeys.push_back(NULL);

        std::vector<const char*> valuePointers;
        valuePointers.reserve(2);
        valuePointers.push_back(iniValues[0].c_str());
        valuePointers.push_back(NULL);

        const char* section = (kval >= KipConfigValue_hpMode) ? CONFIG_KIP_SECTION : CONFIG_VAL_SECTION;
        if (!ini_putsection(section, iniKeys.data(), valuePointers.data(), gPath.c_str())) {
            return false;
        }

        if (immediate) {
            configValues[kval] = value;
        }

        return true;
    }

}
