/*
 *
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
#pragma once
#include "../../ipc.h"
#include "base_menu_gui.h"
#include <set>
#include <unordered_map>
#include <string>
#include <vector>
#include "freq_choice_gui.h"
#include "value_choice_gui.h"
class MiscGui : public BaseMenuGui
{
public:
    MiscGui();
    ~MiscGui();
    void listUI() override;
    void refresh() override;
    
protected:
    SysClkConfigValueList* configList;
    std::map<SysClkConfigValue, tsl::elm::ListItem*> configButtons;
    std::map<SysClkConfigValue, ValueRange> configRanges;
    std::map<SysClkConfigValue, std::vector<NamedValue>> configNamedValues;
    std::map<SysClkConfigValue, tsl::elm::ToggleListItem*> configToggles;
    std::map<SysClkConfigValue, std::tuple<tsl::elm::TrackBar*, tsl::elm::ListItem*, std::vector<uint64_t>>> configTrackbars;
    std::set<SysClkConfigValue> configButtonSKeys;
    std::map<SysClkConfigValue, std::string> configButtonSSubtext;
    
    void addConfigToggle(SysClkConfigValue configVal, const char* altName);
    void addConfigButton(SysClkConfigValue configVal, 
        const char* altName, 
        const ValueRange& range,
        const std::string& categoryName,
        const ValueThresholds* thresholds,
        const std::map<uint32_t, std::string>& labels = {},
        const std::vector<NamedValue>& namedValues = {},
        bool showDefaultValue = true);
        
    void addConfigButtonS(SysClkConfigValue configVal, 
        const char* altName, 
        const ValueRange& range,
        const std::string& categoryName,
        const ValueThresholds* thresholds,
        const std::map<uint32_t, std::string>& labels = {},
        const std::vector<NamedValue>& namedValues = {},
        bool showDefaultValue = true,
        const char* subText = nullptr);
    void addFreqButton(SysClkConfigValue configVal,
                            const char* altName,
                            SysClkModule module,
                            const std::map<uint32_t, std::string>& labels = {});
    void updateConfigToggles();
    
    tsl::elm::ToggleListItem* enabledToggle;
    u8 frameCounter = 60;
};