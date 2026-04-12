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


#include "freq_choice_gui.h"

#include "../format.h"
#include "fatal_gui.h"

FreqChoiceGui::FreqChoiceGui(std::uint32_t selectedHz,
                             std::uint32_t* hzList,
                             std::uint32_t hzCount,
                             HocClkModule module,
                             FreqChoiceListener listener,
                             bool checkMax,
                             std::map<uint32_t, std::string> labels)
{
    this->selectedHz = selectedHz;
    this->hzList = hzList;
    this->hzCount = hzCount;
    this->module = module;
    this->listener = listener;
    this->checkMax = checkMax;
    this->labels = labels;
    this->configList = new HocClkConfigValueList {};
}

FreqChoiceGui::~FreqChoiceGui()
{
    delete this->configList;
}

tsl::elm::ListItem* FreqChoiceGui::createFreqListItem(std::uint32_t hz, bool selected, int safety)
{
    std::string text;
    if(module == HocClkModule_MEM)
        text = formatListFreqHzMem(hz, (RamDisplayUnit)this->configList->values[HocClkConfigValue_RamDisplayUnit]);
    else
        text = formatListFreqHz(hz);

    std::string rightText = "";
    auto it = labels.find(hz);
    if (it != labels.end())
        rightText = it->second;

    if (selected)
        const_cast<std::string&>(rightText) = "\uE14B";

    tsl::elm::ListItem* listItem =
        new tsl::elm::ListItem(text, rightText, false);

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

    listItem->setClickListener([this, hz](u64 keys)
    {
        if ((keys & HidNpadButton_A) == HidNpadButton_A && this->listener) {
            if (this->listener(hz)) {
                tsl::goBack();
            }
            return true;
        }
        return false;
    });

    return listItem;
}

void FreqChoiceGui::listUI()
{
    hocclkIpcGetConfigValues(this->configList);

    // Header based on CPU/GPU/MEM module
    std::string moduleName = hocclkFormatModule(this->module, false);
    this->listElement->addItem(new tsl::elm::CategoryHeader(moduleName));

    // Default option
    this->listElement->addItem(
        this->createFreqListItem(0, this->selectedHz == 0, 0));

    for (std::uint32_t i = 0; i < this->hzCount; i++)
    {
        std::uint32_t hz = this->hzList[i];
        uint32_t mhz = hz / 1000000;

        // if (checkMax && IsMariko()) {
        //     if (moduleName == "cpu" &&
        //         this->configList->values[HocClkConfigValue_MarikoMaxCpuClock] < mhz)
        //         continue;

        //     // if (moduleName == "gpu" &&
        //     //     this->configList->values[HocClkConfigValue_MarikoMaxGpuClock] < mhz)
        //     //     continue;

        //     // if (moduleName == "mem" &&
        //     //     this->configList->values[HocClkConfigValue_MarikoMaxMemClock] < mhz)
        //     //     continue;

        if (checkMax && IsErista())
            if (moduleName == "cpu" && this->configList->values[HocClkConfigValue_EristaMaxCpuClock] < mhz)
                continue;

        //     // if (moduleName == "gpu" &&
        //     //     this->configList->values[HocClkConfigValue_EristaMaxGpuClock] < mhz)
        //     //     continue;

        //     // if (moduleName == "mem" &&
        //     //     this->configList->values[HocClkConfigValue_EristaMaxMemClock] < mhz)
        //     //     continue;
        // }

        if (moduleName == "mem" && mhz <= 600)
            continue;

        uint32_t unsafe_cpu;
        uint32_t unsafe_gpu;
        uint32_t danger_cpu;
        uint32_t danger_gpu;

        if (IsMariko())
        {
            unsafe_cpu = this->configList->values[KipConfigValue_marikoCpuUVHigh] ? 2398 : 1964;
            if(this->configList->values[KipConfigValue_marikoGpuUV] == 0) {
                unsafe_gpu = 1076;
            } else if (this->configList->values[KipConfigValue_marikoGpuUV] == 1) {
                unsafe_gpu = 1153;
            } else {
                unsafe_gpu = 1229;
            }
            danger_cpu = this->configList->values[KipConfigValue_marikoCpuUVHigh] ? 2500 : 2398;
            danger_gpu = 1306;
        }
        else
        {
            unsafe_cpu = this->configList->values[KipConfigValue_eristaCpuUV] ? 2092 : 1786;
            if(this->configList->values[KipConfigValue_eristaGpuUV] == 0) {
                unsafe_gpu = 922;
            } else {
                unsafe_gpu = 961;
            }
            danger_cpu = this->configList->values[KipConfigValue_eristaCpuUV] ? 2194 : 1964;
            danger_gpu = 999;
        }

        int safety = 0;

        if (moduleName == "cpu") {

            if (mhz >= danger_cpu)
                safety = 2;
            else if (mhz >= unsafe_cpu)
                safety = 1;
            else
                safety = 0;

        } else if (moduleName == "gpu") {

            if (mhz >= danger_gpu)
                safety = 2;
            else if (mhz >= unsafe_gpu)
                safety = 1;
            else
                safety = 0;

        } else if (moduleName == "mem") {

            safety = 0;

        }

        this->listElement->addItem(
            this->createFreqListItem(
                hz,
                (mhz == this->selectedHz / 1000000),
                safety
            )
        );
    }

    this->listElement->jumpToItem("", "");
}