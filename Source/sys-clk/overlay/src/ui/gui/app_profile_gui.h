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
#include "../../ipc.h"
#include "base_menu_gui.h"
#include "freq_choice_gui.h"
#include "value_choice_gui.h"
#define SYSCLK_GLOBAL_PROFILE_TID       0xA111111111111111
class AppProfileGui : public BaseMenuGui
{
    protected:
        std::uint64_t applicationId;
        SysClkTitleProfileList* profileList;
        void openFreqChoiceGui(tsl::elm::ListItem* listItem, SysClkProfile profile, SysClkModule module);
        void addModuleListItem(SysClkProfile profile, SysClkModule module);
        void addModuleListItemToggle(SysClkProfile profile, SysClkModule module);
        void openValueChoiceGui(
            tsl::elm::ListItem* listItem,
            std::uint32_t currentValue,
            const ValueRange& range,
            const std::string& categoryName,
            ValueChoiceListener listener,
            const ValueThresholds& thresholds = ValueThresholds(),
            bool enableThresholds = false,
            const std::map<std::uint32_t, std::string>& labels = {},
            const std::vector<NamedValue>& namedValues = {},
            bool showDefaultValue = true
        );
        std::string formatValueDisplay(
            std::uint32_t value,
            const std::vector<NamedValue>& namedValues,
            const std::string& suffix,
            std::uint32_t divisor,
            int decimalPlaces
        );
        void addModuleListItemValue(
            SysClkProfile profile,
            SysClkModule module,
            const std::string& categoryName,
            std::uint32_t min,
            std::uint32_t max,
            std::uint32_t step,
            const std::string& suffix,
            std::uint32_t divisor,
            int decimalPlaces,
            ValueThresholds thresholds,
            std::vector<NamedValue> namedValues = {},
            bool showDefaultValue = true
        );
        void addGovernorSection(SysClkProfile profile);
        void addProfileUI(SysClkProfile profile);
    public:
        AppProfileGui(std::uint64_t applicationId, SysClkTitleProfileList* profileList);
        ~AppProfileGui();
        void listUI() override;
        static void changeTo(std::uint64_t applicationId);
        void update() override;
};