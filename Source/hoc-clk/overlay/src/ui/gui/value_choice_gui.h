
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
#include <list>
#include <functional>
#include <string>
#include <map>
#include <vector>
#include "base_menu_gui.h"

using ValueChoiceListener = std::function<bool(std::uint32_t value)>;
#define VALUE_DEFAULT_TEXT "Default"

struct ValueRange {
    std::uint32_t min;
    std::uint32_t max;
    std::uint32_t step;
    std::string suffix;
    std::uint32_t divisor;
    int decimalPlaces;
    ValueRange()
        : min(0), max(0), step(1), suffix(""), divisor(1), decimalPlaces(0) {}
    ValueRange(std::uint32_t min, std::uint32_t max, std::uint32_t step,
               const std::string& suffix = "", std::uint32_t divisor = 1, int decimalPlaces = 0)
        : min(min), max(max), step(step), suffix(suffix),
          divisor(divisor), decimalPlaces(decimalPlaces) {}
};

struct ValueThresholds {
    std::uint32_t warning;
    std::uint32_t danger;
    ValueThresholds(std::uint32_t warning = 0, std::uint32_t danger = 0)
        : warning(warning), danger(danger) {}
};

struct NamedValue {
    std::string name;
    std::uint32_t value;
    std::string rightText;
    
    NamedValue(const std::string& name, std::uint32_t value, const std::string& rightText = "")
        : name(name), value(value), rightText(rightText) {}
};

class ValueChoiceGui : public BaseMenuGui
{
protected:
    std::uint32_t selectedValue;
    ValueRange range;
    std::string categoryName;
    ValueChoiceListener listener;
    ValueThresholds thresholds;
    bool enableThresholds;
    std::map<std::uint32_t, std::string> labels;
    
    std::vector<NamedValue> namedValues;
    bool showDefaultValue = true;
    bool showDNO = false;
    tsl::elm::ListItem* createValueListItem(std::uint32_t value, bool selected, int safety);
    tsl::elm::ListItem* createNamedValueListItem(const NamedValue& namedValue, bool selected, int safety);
    std::string formatValue(std::uint32_t value);
    int getSafetyLevel(std::uint32_t value);
    
public:
    ValueChoiceGui(std::uint32_t selectedValue,
                   const ValueRange& range,
                   const std::string& categoryName,
                   ValueChoiceListener listener,
                   const ValueThresholds& thresholds = ValueThresholds(),
                   bool enableThresholds = false,
                   std::map<std::uint32_t, std::string> labels = {},
                   std::vector<NamedValue> namedValues = {},
                   bool showDefaultValue = true,
                   bool showDNO = false);
    ~ValueChoiceGui();
    
    void addNamedValue(const std::string& name, std::uint32_t value, const std::string& rightText = "")
    {
        namedValues.emplace_back(name, value, rightText);
    }
    
    void addNamedValues(const std::vector<NamedValue>& values)
    {
        namedValues.insert(namedValues.end(), values.begin(), values.end());
    }
    
    void clearNamedValues()
    {
        namedValues.clear();
    }
    
    void setShowDefaultValue(bool show)
    {
        showDefaultValue = show;
    }
    
    void listUI() override;
};