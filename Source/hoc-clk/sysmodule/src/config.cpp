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

namespace config {

    uint64_t configValues[SysClkConfigValue_EnumMax];

    namespace {

        bool gLoaded = false;
        std::string gPath;
        time_t gMtime = 0;
        std::atomic_bool gEnabled{false};
        std::uint32_t gOverrideFreqs[SysClkModule_EnumMax];
        std::map<std::tuple<std::uint64_t, SysClkProfile, SysClkModule>, std::uint32_t> gProfileMHzMap;
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

        std::uint32_t FindClockMHz(std::uint64_t tid, SysClkModule module, SysClkProfile profile) {
            if (gLoaded) {
                auto it = gProfileMHzMap.find(std::make_tuple(tid, profile, module));
                if (it != gProfileMHzMap.end()) {
                    return it->second;
                }
            }
            return 0;
        }

        std::uint32_t FindClockHzFromProfiles(std::uint64_t tid, SysClkModule module, std::initializer_list<SysClkProfile> profiles, u32 mhzMultiplier = 1000000) {
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
            if (!strcmp(section, CONFIG_VAL_SECTION)) {
                for (unsigned int kval = 0; kval < SysClkConfigValue_EnumMax; kval++) {
                    if (!strcmp(key, sysclkFormatConfigValue((SysClkConfigValue)kval, false))) {
                        input = strtoul(value, NULL, 0);
                        if (!sysclkValidConfigValue((SysClkConfigValue)kval, input)) {
                            input = sysclkDefaultConfigValue((SysClkConfigValue)kval);
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

            SysClkProfile parsedProfile = SysClkProfile_EnumMax;
            SysClkModule parsedModule = SysClkModule_EnumMax;

            for (unsigned int profile = 0; profile < SysClkProfile_EnumMax; profile++) {
                const char* profileCode = board::GetProfileName((SysClkProfile)profile, false);
                size_t profileCodeLen = strlen(profileCode);

                if (!strncmp(key, profileCode, profileCodeLen) && key[profileCodeLen] == '_') {
                    const char* subkey = key + profileCodeLen + 1;

                    for (unsigned int module = 0; module < SysClkModule_EnumMax; module++) {
                        const char* moduleCode = board::GetModuleName((SysClkModule)module, false);
                        size_t moduleCodeLen = strlen(moduleCode);
                        if (!strncmp(subkey, moduleCode, moduleCodeLen) && subkey[moduleCodeLen] == '\0') {
                            parsedProfile = (SysClkProfile)profile;
                            parsedModule = (SysClkModule)module;
                        }
                    }
                }
            }

            if (parsedModule == SysClkModule_EnumMax || parsedProfile == SysClkProfile_EnumMax) {
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
            for (unsigned int i = 0; i < SysClkConfigValue_EnumMax; i++) {
                configValues[i] = sysclkDefaultConfigValue((SysClkConfigValue)i);
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
        for (unsigned int i = 0; i < SysClkModule_EnumMax; i++) {
            gOverrideFreqs[i] = 0;
        }
        for (unsigned int i = 0; i < SysClkConfigValue_EnumMax; i++) {
            configValues[i] = sysclkDefaultConfigValue((SysClkConfigValue)i);
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

    bool HasProfilesLoaded() {
        std::scoped_lock lock{gConfigMutex};
        return gLoaded;
    }

    std::uint32_t GetAutoClockHz(std::uint64_t tid, SysClkModule module, SysClkProfile profile, bool returnRaw) {
        std::scoped_lock lock{gConfigMutex};
        switch (profile) {
            case SysClkProfile_Handheld:
                return FindClockHzFromProfiles(tid, module, {SysClkProfile_Handheld}, returnRaw ? 1 : 1000000);
            case SysClkProfile_HandheldCharging:
            case SysClkProfile_HandheldChargingUSB:
                return FindClockHzFromProfiles(tid, module, {SysClkProfile_HandheldChargingUSB, SysClkProfile_HandheldCharging, SysClkProfile_Handheld}, returnRaw ? 1 : 1000000);
            case SysClkProfile_HandheldChargingOfficial:
                return FindClockHzFromProfiles(tid, module, {SysClkProfile_HandheldChargingOfficial, SysClkProfile_HandheldCharging, SysClkProfile_Handheld}, returnRaw ? 1 : 1000000);
            case SysClkProfile_Docked:
                return FindClockHzFromProfiles(tid, module, {SysClkProfile_Docked}, returnRaw ? 1 : 1000000);
            default:
                ERROR_THROW("Unhandled SysClkProfile: %u", profile);
        }
        return 0;
    }

    void GetProfiles(std::uint64_t tid, SysClkTitleProfileList* out_profiles) {
        std::scoped_lock lock{gConfigMutex};
        for (unsigned int profile = 0; profile < SysClkProfile_EnumMax; profile++) {
            for (unsigned int module = 0; module < SysClkModule_EnumMax; module++) {
                out_profiles->mhzMap[profile][module] = FindClockMHz(tid, (SysClkModule)module, (SysClkProfile)profile);
            }
        }
    }

    bool SetProfiles(std::uint64_t tid, SysClkTitleProfileList* profiles, bool immediate) {
        std::scoped_lock lock{gConfigMutex};
        uint8_t numProfiles = 0;

        char section[17] = {0};
        snprintf(section, sizeof(section), "%016lX", tid);

        std::vector<std::string> keys;
        std::vector<std::string> values;
        keys.reserve(SysClkProfile_EnumMax * SysClkModule_EnumMax);
        values.reserve(SysClkProfile_EnumMax * SysClkModule_EnumMax);

        std::uint32_t* mhz = &profiles->mhz[0];

        for (unsigned int profile = 0; profile < SysClkProfile_EnumMax; profile++) {
            for (unsigned int module = 0; module < SysClkModule_EnumMax; module++) {
                if (*mhz) {
                    numProfiles++;

                    std::string key = std::string(board::GetProfileName((SysClkProfile)profile, false)) +
                                      "_" +
                                      board::GetModuleName((SysClkModule)module, false);
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
            for (unsigned int profile = 0; profile < SysClkProfile_EnumMax; profile++) {
                for (unsigned int module = 0; module < SysClkModule_EnumMax; module++) {
                    if (*mhz) {
                        gProfileMHzMap[std::make_tuple(tid, (SysClkProfile)profile, (SysClkModule)module)] = *mhz;
                    } else {
                        gProfileMHzMap.erase(std::make_tuple(tid, (SysClkProfile)profile, (SysClkModule)module));
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

    void SetOverrideHz(SysClkModule module, std::uint32_t hz) {
        ASSERT_ENUM_VALID(SysClkModule, module);
        std::scoped_lock lock{gOverrideMutex};
        gOverrideFreqs[module] = hz;
    }

    std::uint32_t GetOverrideHz(SysClkModule module) {
        ASSERT_ENUM_VALID(SysClkModule, module);
        std::scoped_lock lock{gOverrideMutex};
        return gOverrideFreqs[module];
    }

    std::uint64_t GetConfigValue(SysClkConfigValue kval) {
        ASSERT_ENUM_VALID(SysClkConfigValue, kval);
        std::scoped_lock lock{gConfigMutex};
        return configValues[kval];
    }

    const char* GetConfigValueName(SysClkConfigValue kval, bool pretty) {
        ASSERT_ENUM_VALID(SysClkConfigValue, kval);
        return sysclkFormatConfigValue(kval, pretty);
    }

    void GetConfigValues(SysClkConfigValueList* out_configValues) {
        std::scoped_lock lock{gConfigMutex};
        for (unsigned int kval = 0; kval < SysClkConfigValue_EnumMax; kval++) {
            out_configValues->values[kval] = configValues[kval];
        }
    }

    bool SetConfigValues(SysClkConfigValueList* configValues, bool immediate) {
        std::scoped_lock lock{gConfigMutex};

        std::vector<const char*> iniKeys;
        std::vector<std::string> iniValues;
        iniKeys.reserve(SysClkConfigValue_EnumMax + 1);
        iniValues.reserve(SysClkConfigValue_EnumMax);

        for (unsigned int kval = 0; kval < SysClkConfigValue_EnumMax; kval++) {
            if (!sysclkValidConfigValue((SysClkConfigValue)kval, configValues->values[kval]) ||
               configValues->values[kval] == sysclkDefaultConfigValue((SysClkConfigValue)kval)) {
                continue;
            }
            iniValues.push_back(std::to_string(configValues->values[kval]));
            iniKeys.push_back(sysclkFormatConfigValue((SysClkConfigValue)kval, false));
        }

        iniKeys.push_back(NULL);

        std::vector<const char*> valuePointers;
        valuePointers.reserve(iniValues.size() + 1);
        for (const auto& val : iniValues) {
            valuePointers.push_back(val.c_str());
        }
        valuePointers.push_back(NULL);

        if (!ini_putsection(CONFIG_VAL_SECTION, iniKeys.data(), valuePointers.data(), gPath.c_str())) {
            return false;
        }

        if (immediate) {
            for (unsigned int kval = 0; kval < SysClkConfigValue_EnumMax; kval++) {
                if (sysclkValidConfigValue((SysClkConfigValue)kval, configValues->values[kval])) {
                    config::configValues[kval] = configValues->values[kval];
                } else {
                    config::configValues[kval] = sysclkDefaultConfigValue((SysClkConfigValue)kval);
                }
            }
        }

        return true;
    }

    bool ResetConfigValue(SysClkConfigValue kval) {
        if (!SYSCLK_ENUM_VALID(SysClkConfigValue, kval)) {
            fileUtils::LogLine("[cfg] Invalid SysClkConfigValue: %u", kval);
            return false;
        }

        std::scoped_lock lock{gConfigMutex};

        std::uint64_t defaultValue = sysclkDefaultConfigValue(kval);

        std::vector<const char*> iniKeys;
        std::vector<std::string> iniValues;
        iniKeys.reserve(2);
        iniValues.reserve(1);

        iniKeys.push_back(sysclkFormatConfigValue(kval, false));
        iniValues.push_back("");
        iniKeys.push_back(NULL);

        std::vector<const char*> valuePointers;
        valuePointers.reserve(iniValues.size() + 1);
        for (const auto& val : iniValues) {
            valuePointers.push_back(val.c_str());
        }
        valuePointers.push_back(NULL);

        if (!ini_putsection(CONFIG_VAL_SECTION, iniKeys.data(), valuePointers.data(), gPath.c_str())) {
            fileUtils::LogLine("[cfg] Failed to reset config value %u in INI", kval);
            return false;
        }

        configValues[kval] = defaultValue;
        fileUtils::LogLine("[cfg] Reset config value %u to default: %llu", kval, defaultValue);

        return true;
    }

    bool SetConfigValue(SysClkConfigValue kval, std::uint64_t value, bool immediate) {
        if (!SYSCLK_ENUM_VALID(SysClkConfigValue, kval)) {
            return false;
        }
        if (!sysclkValidConfigValue(kval, value)) {
            return false;
        }

        std::scoped_lock lock{gConfigMutex};

        std::vector<const char*> iniKeys;
        std::vector<std::string> iniValues;
        iniKeys.reserve(2);
        iniValues.reserve(1);

        iniKeys.push_back(sysclkFormatConfigValue(kval, false));
        iniValues.push_back(std::to_string(value));
        iniKeys.push_back(NULL);

        std::vector<const char*> valuePointers;
        valuePointers.reserve(2);
        valuePointers.push_back(iniValues[0].c_str());
        valuePointers.push_back(NULL);

        if (!ini_putsection(CONFIG_VAL_SECTION, iniKeys.data(), valuePointers.data(), gPath.c_str())) {
            return false;
        }

        if (immediate) {
            configValues[kval] = value;
        }

        return true;
    }

}
