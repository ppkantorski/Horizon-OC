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
#include <string>
#include "value_choice_gui.h"
class GlobalOverrideGui : public BaseMenuGui
{
    protected:
        std::map<SysClkModule, std::tuple<std::string, std::uint32_t, int>> customFormatModules;
        tsl::elm::ListItem* listItems[SysClkModule_EnumMax];
        std::uint32_t listHz[SysClkModule_EnumMax];
        void openFreqChoiceGui(SysClkModule module);
        void addGovernorSection();
        void addModuleListItem(SysClkModule module);
        void addModuleToggleItem(SysClkModule module);
        void openValueChoiceGui(
            tsl::elm::ListItem* listItem,
            std::uint32_t currentValue,
            const ValueRange& range,
            const std::string& categoryName,
            ValueChoiceListener listener,
            const ValueThresholds& thresholds,
            bool enableThresholds,
            const std::map<std::uint32_t, std::string>& labels,
            const std::vector<NamedValue>& namedValues,
            bool showDefaultValue
        );
        void addModuleListItemValue(
            SysClkModule module,
            const std::string& categoryName,
            std::uint32_t min,
            std::uint32_t max,
            std::uint32_t step,
            const std::string& suffix,
            std::uint32_t divisor,
            int decimalPlaces,
            ValueThresholds thresholds = {},
            const std::vector<NamedValue>& namedValues = {},
            bool showDefaultValue = true
        );
    public:
        GlobalOverrideGui();
        ~GlobalOverrideGui() {}
        void listUI() override;
        void refresh() override;
        void setModuleCustomFormat(SysClkModule module, const std::string& suffix, std::uint32_t divisor, int decimalPlaces);
};