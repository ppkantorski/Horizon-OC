
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
#include "value_choice_gui.h"
#include "../format.h"
#include "fatal_gui.h"
#include <sstream>
#include <iomanip>

ValueChoiceGui::ValueChoiceGui(std::uint32_t selectedValue,
                               const ValueRange& range,
                               const std::string& categoryName,
                               ValueChoiceListener listener,
                               const ValueThresholds& thresholds,
                               bool enableThresholds,
                               std::map<std::uint32_t, std::string> labels,
                               std::vector<NamedValue> namedValues,
                               bool showDefaultValue,
                               bool showDNO)
    : selectedValue(selectedValue),
      range(range),
      categoryName(categoryName),
      listener(listener),
      thresholds(thresholds),
      enableThresholds(enableThresholds),
      labels(labels),
      namedValues(namedValues),
      showDefaultValue(showDefaultValue),
      showDNO(showDNO)
{
}

ValueChoiceGui::~ValueChoiceGui()
{
}

std::string ValueChoiceGui::formatValue(std::uint32_t value)
{
    std::ostringstream oss;
    if(showDefaultValue) {
        if (value == 0) {
            return this->showDNO ? FREQ_DEFAULT_TEXT : VALUE_DEFAULT_TEXT;
        }
    }
    double displayValue = static_cast<double>(value) / static_cast<double>(range.divisor);
    oss << std::fixed << std::setprecision(range.decimalPlaces) << displayValue;
    if (!range.suffix.empty()) {
        oss << " " << range.suffix;
    }
    return oss.str();
}

int ValueChoiceGui::getSafetyLevel(std::uint32_t value)
{
    if(thresholds.warning == 0 && thresholds.danger == 0) {
        return 0;
    }
    
    if (value > thresholds.danger) {
        return 2;
    }
    if (value > thresholds.warning) {
        return 1;
    }
    return 0;
}

tsl::elm::ListItem* ValueChoiceGui::createValueListItem(std::uint32_t value, bool selected, int safety)
{
    std::string text = formatValue(value);
    std::string rightText = "";

    auto it = labels.find(value);
    if (it != labels.end()) {
        rightText = it->second;
    }

    if (selected) {
        const_cast<std::string&>(rightText) = "\uE14B";
    }

    tsl::elm::ListItem* listItem = new tsl::elm::ListItem(text, rightText, false);
    switch (safety)
    {
    case 0:
        listItem->setTextColor(tsl::Color(255, 255, 255, 255));
        listItem->setValueColor(tsl::Color(255, 255, 255, 255));
        break;
    case 1:
        listItem->setTextColor(tsl::Color(255, 165, 0, 255));
        listItem->setValueColor(tsl::Color(255, 165, 0, 255));
        break;
    case 2:
        listItem->setTextColor(tsl::Color(255, 0, 0, 255));
        listItem->setValueColor(tsl::Color(255, 0, 0, 255));
        break;
    }

    // Make annotation grey
    if (!rightText.empty() && !selected)
        listItem->setValueColor(tsl::Color(180, 180, 180, 255));
    else if(selected)
        listItem->setValueColor(tsl::infoTextColor);    
        
    listItem->setClickListener([this, value](u64 keys)
    {
        if ((keys & HidNpadButton_A) == HidNpadButton_A && this->listener) {
            if (this->listener(value)) {
                tsl::goBack();
            }
            return true;
        }
        return false;
    });
    return listItem;
}

tsl::elm::ListItem* ValueChoiceGui::createNamedValueListItem(const NamedValue& namedValue, bool selected, int safety)
{
    std::string text = namedValue.name;
    if (selected) {
        const_cast<std::string&>(namedValue.rightText) = "\uE14B";
    }
    
    tsl::elm::ListItem* listItem = new tsl::elm::ListItem(text, namedValue.rightText, false);
    switch (safety)
    {
    case 0:
        listItem->setTextColor(tsl::Color(255, 255, 255, 255));
        listItem->setValueColor(tsl::Color(255, 255, 255, 255));
        break;
    case 1:
        listItem->setTextColor(tsl::Color(255, 165, 0, 255));
        listItem->setValueColor(tsl::Color(255, 165, 0, 255));
        break;
    case 2:
        listItem->setTextColor(tsl::Color(255, 0, 0, 255));
        listItem->setValueColor(tsl::Color(255, 0, 0, 255));
        break;
    }

    if (!namedValue.rightText.empty() && !selected)
        listItem->setValueColor(tsl::Color(180, 180, 180, 255));
    else if(selected)
        listItem->setValueColor(tsl::infoTextColor);    

    listItem->setClickListener([this, value = namedValue.value](u64 keys)
    {
        if ((keys & HidNpadButton_A) == HidNpadButton_A && this->listener) {
            if (this->listener(value)) {
                tsl::goBack();
            }
            return true;
        }
        return false;
    });
    return listItem;
}

void ValueChoiceGui::listUI()
{
    if (!categoryName.empty()) {
        this->listElement->addItem(new tsl::elm::CategoryHeader(categoryName));
    }

    if (showDefaultValue) {
        this->listElement->addItem(this->createValueListItem(0, this->selectedValue == 0, 0));
    }
    for (const auto& namedValue : namedValues) {
        int safety = enableThresholds ? getSafetyLevel(namedValue.value) : 0;
        bool selected = (namedValue.value == this->selectedValue);
        this->listElement->addItem(this->createNamedValueListItem(namedValue, selected, safety));
    }
    
    if (namedValues.empty()) {
        for (std::uint32_t value = range.min; value <= range.max; value += range.step)
        {
            int safety = getSafetyLevel(value);
            bool selected = (value == this->selectedValue);
            this->listElement->addItem(this->createValueListItem(value, selected, safety));
        }
    }
    
    this->listElement->jumpToItem("", "\uE14B");
}