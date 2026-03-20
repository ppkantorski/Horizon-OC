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


#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sstream>
#include <algorithm>
#include <cstring>
#include "errors.h"
#include "file_utils.h"

Config::Config(std::string path)
{
    this->path = path;
    this->loaded = false;
    this->profileMHzMap = std::map<std::tuple<std::uint64_t, SysClkProfile, SysClkModule>, std::uint32_t>();
    this->profileCountMap = std::map<std::uint64_t, std::uint8_t>();
    this->mtime = 0;
    this->enabled = false;
    for(unsigned int i = 0; i < SysClkModule_EnumMax; i++)
    {
        this->overrideFreqs[i] = 0;
    }

    for(unsigned int i = 0; i < SysClkConfigValue_EnumMax; i++)
    {
        this->configValues[i] = sysclkDefaultConfigValue((SysClkConfigValue)i);
    }
}

Config::~Config()
{
    std::scoped_lock lock{this->configMutex};
    this->Close();
}

Config* Config::CreateDefault()
{
    return new Config(FILE_CONFIG_DIR "/config.ini");
}

void Config::Load()
{
    FileUtils::LogLine("[cfg] Reading %s", this->path.c_str());

    this->Close();
    this->mtime = this->CheckModificationTime();
    if(!this->mtime)
    {
        FileUtils::LogLine("[cfg] Error finding file");
    }
    else if (!ini_browse(&BrowseIniFunc, this, this->path.c_str()))
    {
        FileUtils::LogLine("[cfg] Error loading file");
    }

    this->loaded = true;
}

void Config::Close()
{
    this->loaded = false;
    this->profileMHzMap.clear();
    this->profileCountMap.clear();

    for(unsigned int i = 0; i < SysClkConfigValue_EnumMax; i++)
    {
        this->configValues[i] = sysclkDefaultConfigValue((SysClkConfigValue)i);
    }
}

bool Config::Refresh()
{
    std::scoped_lock lock{this->configMutex};
    if (!this->loaded || this->mtime != this->CheckModificationTime())
    {
        this->Load();
        return true;
    }
    return false;
}

bool Config::HasProfilesLoaded()
{
    std::scoped_lock lock{this->configMutex};
    return this->loaded;
}

time_t Config::CheckModificationTime()
{
    time_t mtime = 0;
    struct stat st;
    if (stat(this->path.c_str(), &st) == 0)
    {
        mtime = st.st_mtime;
    }

    return mtime;
}

std::uint32_t Config::FindClockMHz(std::uint64_t tid, SysClkModule module, SysClkProfile profile)
{
    if (this->loaded)
    {
        std::map<std::tuple<std::uint64_t, SysClkProfile, SysClkModule>, std::uint32_t>::const_iterator it = this->profileMHzMap.find(std::make_tuple(tid, profile, module));
        if (it != this->profileMHzMap.end())
        {
            return it->second;
        }
    }

    return 0;
}

std::uint32_t Config::FindClockHzFromProfiles(std::uint64_t tid, SysClkModule module, std::initializer_list<SysClkProfile> profiles, u32 mhzMultiplier)
{
    std::uint32_t mhz = 0;

    if (this->loaded)
    {
        for(auto profile: profiles)
        {
            mhz = FindClockMHz(tid, module, profile);

            if(mhz)
            {
                break;
            }
        }
    }

    return std::max((std::uint32_t)0, mhz * mhzMultiplier);
}

std::uint32_t Config::GetAutoClockHz(std::uint64_t tid, SysClkModule module, SysClkProfile profile, bool returnRaw)
{
    std::scoped_lock lock{this->configMutex};
    switch(profile)
    {
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

void Config::GetProfiles(std::uint64_t tid, SysClkTitleProfileList* out_profiles)
{
    std::scoped_lock lock{this->configMutex};

    for(unsigned int profile = 0; profile < SysClkProfile_EnumMax; profile++)
    {
        for(unsigned int module = 0; module < SysClkModule_EnumMax; module++)
        {
            out_profiles->mhzMap[profile][module] = FindClockMHz(tid, (SysClkModule)module, (SysClkProfile)profile);
        }
    }
}

bool Config::SetProfiles(std::uint64_t tid, SysClkTitleProfileList* profiles, bool immediate)
{
    std::scoped_lock lock{this->configMutex};
    uint8_t numProfiles = 0;

    char section[17] = {0};
    snprintf(section, sizeof(section), "%016lX", tid);

    std::vector<std::string> keys;
    std::vector<std::string> values;
    keys.reserve(SysClkProfile_EnumMax * SysClkModule_EnumMax);
    values.reserve(SysClkProfile_EnumMax * SysClkModule_EnumMax);

    std::uint32_t* mhz = &profiles->mhz[0];

    for(unsigned int profile = 0; profile < SysClkProfile_EnumMax; profile++)
    {
        for(unsigned int module = 0; module < SysClkModule_EnumMax; module++)
        {
            if(*mhz)
            {
                numProfiles++;

                std::string key = std::string(Board::GetProfileName((SysClkProfile)profile, false)) + 
                                  "_" + 
                                  Board::GetModuleName((SysClkModule)module, false);
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

    for(size_t i = 0; i < keys.size(); i++) {
        keyPointers.push_back(keys[i].c_str());
        valuePointers.push_back(values[i].c_str());
    }
    keyPointers.push_back(NULL);
    valuePointers.push_back(NULL);

    if(!ini_putsection(section, keyPointers.data(), valuePointers.data(), this->path.c_str()))
    {
        return false;
    }

    if(immediate)
    {
        mhz = &profiles->mhz[0];
        this->profileCountMap[tid] = numProfiles;
        for(unsigned int profile = 0; profile < SysClkProfile_EnumMax; profile++)
        {
            for(unsigned int module = 0; module < SysClkModule_EnumMax; module++)
            {
                if(*mhz)
                {
                    this->profileMHzMap[std::make_tuple(tid, (SysClkProfile)profile, (SysClkModule)module)] = *mhz;
                }
                else
                {
                    this->profileMHzMap.erase(std::make_tuple(tid, (SysClkProfile)profile, (SysClkModule)module));
                }
                mhz++;
            }
        }
    }

    return true;
}

std::uint8_t Config::GetProfileCount(std::uint64_t tid)
{
    std::map<std::uint64_t, std::uint8_t>::iterator it = this->profileCountMap.find(tid);
    if (it == this->profileCountMap.end())
    {
        return 0;
    }

    return it->second;
}

int Config::BrowseIniFunc(const char* section, const char* key, const char* value, void* userdata)
{
    Config* config = (Config*)userdata;
    std::uint64_t input;
    if(!strcmp(section, CONFIG_VAL_SECTION))
    {
        for(unsigned int kval = 0; kval < SysClkConfigValue_EnumMax; kval++)
        {
            if(!strcmp(key, sysclkFormatConfigValue((SysClkConfigValue)kval, false)))
            {
                input = strtoul(value, NULL, 0);
                if(!sysclkValidConfigValue((SysClkConfigValue)kval, input))
                {
                    input = sysclkDefaultConfigValue((SysClkConfigValue)kval);
                    FileUtils::LogLine("[cfg] Invalid value for key '%s' in section '%s': using default %d", key, section, input);
                }
                config->configValues[kval] = input;
                return 1;
            }
        }

        FileUtils::LogLine("[cfg] Skipping key '%s' in section '%s': Unrecognized config value", key, section);
        return 1;
    }

    std::uint64_t tid = strtoul(section, NULL, 16);

    if(!tid || strlen(section) != 16)
    {
        FileUtils::LogLine("[cfg] Skipping key '%s' in section '%s': Invalid TitleID", key, section);
        return 1;
    }

    SysClkProfile parsedProfile = SysClkProfile_EnumMax;
    SysClkModule parsedModule = SysClkModule_EnumMax;

    for(unsigned int profile = 0; profile < SysClkProfile_EnumMax; profile++)
    {
        const char* profileCode = Board::GetProfileName((SysClkProfile)profile, false);
        size_t profileCodeLen = strlen(profileCode);

        if(!strncmp(key, profileCode, profileCodeLen) && key[profileCodeLen] == '_')
        {
            const char* subkey = key + profileCodeLen + 1;

            for(unsigned int module = 0; module < SysClkModule_EnumMax; module++)
            {
                const char* moduleCode = Board::GetModuleName((SysClkModule)module, false);
                size_t moduleCodeLen = strlen(moduleCode);
                if(!strncmp(subkey, moduleCode, moduleCodeLen) && subkey[moduleCodeLen] == '\0')
                {
                    parsedProfile = (SysClkProfile)profile;
                    parsedModule = (SysClkModule)module;
                }
            }
        }
    }

    if(parsedModule == SysClkModule_EnumMax || parsedProfile == SysClkProfile_EnumMax)
    {
        FileUtils::LogLine("[cfg] Skipping key '%s' in section '%s': Unrecognized key", key, section);
        return 1;
    }

    std::uint32_t mhz = strtoul(value, NULL, 10);
    if(!mhz)
    {
        FileUtils::LogLine("[cfg] Skipping key '%s' in section '%s': Invalid value", key, section);
        return 1;
    }

    config->profileMHzMap[std::make_tuple(tid, parsedProfile, parsedModule)] = mhz;
    std::map<std::uint64_t, std::uint8_t>::iterator it = config->profileCountMap.find(tid);
    if (it == config->profileCountMap.end())
    {
        config->profileCountMap[tid] = 1;
    }
    else
    {
        it->second++;
    }

    return 1;
}

void Config::SetEnabled(bool enabled)
{
    this->enabled = enabled;
}

bool Config::Enabled()
{
    return this->enabled;
}

void Config::SetOverrideHz(SysClkModule module, std::uint32_t hz)
{
    ASSERT_ENUM_VALID(SysClkModule, module);

    std::scoped_lock lock{this->overrideMutex};

    this->overrideFreqs[module] = hz;
}

std::uint32_t Config::GetOverrideHz(SysClkModule module)
{
    ASSERT_ENUM_VALID(SysClkModule, module);

    std::scoped_lock lock{this->overrideMutex};

    return this->overrideFreqs[module];
}

std::uint64_t Config::GetConfigValue(SysClkConfigValue kval)
{
    ASSERT_ENUM_VALID(SysClkConfigValue, kval);

    std::scoped_lock lock{this->configMutex};

    return this->configValues[kval];
}

const char* Config::GetConfigValueName(SysClkConfigValue kval, bool pretty)
{
    ASSERT_ENUM_VALID(SysClkConfigValue, kval);

    const char* result = sysclkFormatConfigValue(kval, pretty);

    return result;
}

void Config::GetConfigValues(SysClkConfigValueList* out_configValues)
{
    std::scoped_lock lock{this->configMutex};

    for(unsigned int kval = 0; kval < SysClkConfigValue_EnumMax; kval++)
    {
        out_configValues->values[kval] = this->configValues[kval];
    }
}

bool Config::SetConfigValues(SysClkConfigValueList* configValues, bool immediate)
{
    std::scoped_lock lock{this->configMutex};

    std::vector<const char*> iniKeys;
    std::vector<std::string> iniValues;
    
    iniKeys.reserve(SysClkConfigValue_EnumMax + 1);
    iniValues.reserve(SysClkConfigValue_EnumMax);

    for(unsigned int kval = 0; kval < SysClkConfigValue_EnumMax; kval++)
    {
        if(!sysclkValidConfigValue((SysClkConfigValue)kval, configValues->values[kval]) || 
           configValues->values[kval] == sysclkDefaultConfigValue((SysClkConfigValue)kval))
        {
            continue;
        }

        iniValues.push_back(std::to_string(configValues->values[kval]));
        iniKeys.push_back(sysclkFormatConfigValue((SysClkConfigValue)kval, false));
    }

    // Null terminate
    iniKeys.push_back(NULL);

    // Build pointer array for ini function
    std::vector<const char*> valuePointers;
    valuePointers.reserve(iniValues.size() + 1);
    for(const auto& val : iniValues) {
        valuePointers.push_back(val.c_str());
    }
    valuePointers.push_back(NULL);

    if(!ini_putsection(CONFIG_VAL_SECTION, iniKeys.data(), valuePointers.data(), this->path.c_str()))
    {
        return false;
    }

    // Only actually apply changes in memory after a successful save
    if(immediate)
    {
        for(unsigned int kval = 0; kval < SysClkConfigValue_EnumMax; kval++)
        {
            if(sysclkValidConfigValue((SysClkConfigValue)kval, configValues->values[kval]))
            {
                this->configValues[kval] = configValues->values[kval];
            }
            else
            {
                this->configValues[kval] = sysclkDefaultConfigValue((SysClkConfigValue)kval);
            }
        }
    }

    return true;
}

bool Config::ResetConfigValue(SysClkConfigValue kval)
{
    if (!SYSCLK_ENUM_VALID(SysClkConfigValue, kval)) {
        FileUtils::LogLine("[cfg] Invalid SysClkConfigValue: %u", kval);
        return false;
    }

    std::scoped_lock lock{this->configMutex};

    std::uint64_t defaultValue = sysclkDefaultConfigValue(kval);

    std::vector<const char*> iniKeys;
    std::vector<std::string> iniValues;
    
    iniKeys.reserve(2);
    iniValues.reserve(1);

    const char* keyStr = sysclkFormatConfigValue(kval, false);
    
    iniKeys.push_back(keyStr);
    iniValues.push_back("");

    iniKeys.push_back(NULL);

    std::vector<const char*> valuePointers;
    valuePointers.reserve(iniValues.size() + 1);
    for (const auto& val : iniValues) {
        valuePointers.push_back(val.c_str());
    }
    valuePointers.push_back(NULL);

    if (!ini_putsection(CONFIG_VAL_SECTION, iniKeys.data(), valuePointers.data(), this->path.c_str())) {
        FileUtils::LogLine("[cfg] Failed to reset config value %u in INI", kval);
        return false;
    }

    this->configValues[kval] = defaultValue;
    FileUtils::LogLine("[cfg] Reset config value %u to default: %llu", kval, defaultValue);
    
    return true;
}