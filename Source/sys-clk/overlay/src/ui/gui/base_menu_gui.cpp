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


#include "base_menu_gui.h"
#include "fatal_gui.h"

// Cache hardware model to avoid repeated syscalls

BaseMenuGui::BaseMenuGui() : tempColors{ tsl::Color(0), tsl::Color(0), tsl::Color(0), tsl::Color(0), tsl::Color(0), tsl::Color(0), tsl::Color(0), }
{
    tsl::initializeThemeVars();
    this->context = nullptr;
    this->lastContextUpdate = 0;
    this->listElement = nullptr;


    // Pre-cache hardware model during initialization
    IsAula();
    IsMariko();
    IsHoag();

    // Initialize display strings
    memset(displayStrings, 0, sizeof(displayStrings));
}

BaseMenuGui::~BaseMenuGui() {
    delete this->context; // delete handles nullptr automatically
}

// Fast preDraw - just renders pre-computed strings
void BaseMenuGui::preDraw(tsl::gfx::Renderer* renderer) {
    BaseGui::preDraw(renderer);
    if(!this->context) [[unlikely]] return;

    // All constants pre-calculated and cached
    static constexpr const char* const labels[] = {
        "App ID", "Profile", "CPU", "GPU", "MEM", "SoC", "Board", "Skin", "Now", "Avg", "BAT", "PMIC", "FAN", "DISP", "FPS"
    };

    static constexpr u32 dataPositions[6] = {63-3+3, 200-1, 344-1-3, 200-1, 342-1, 321-1};

    static u32 labelWidths[10];
    static bool positionsInitialized = false;

    if (!positionsInitialized) {
        for (int i = 0; i < 10; i++) {
            labelWidths[i] = renderer->getTextDimensions(labels[i], false, SMALL_TEXT_SIZE).first;
        }
        positionsInitialized = true;
    }
    static u32 positions[10] = {24-1, 310-labelWidths[1], 24-1, 192-labelWidths[3], 332-labelWidths[4], 24-1, 192 - labelWidths[6], 332-labelWidths[7], 192 - labelWidths[8], 332-labelWidths[9]};

    static u32 maxProfileValueWidth = renderer->getTextDimensions("USB Charger", false, SMALL_TEXT_SIZE).first; // longest word

    u32 y = 91;

    // === TOP SECTION ===
    renderer->drawRoundedRect(14, 70-1, 420, 30+2, 12.0f, renderer->aWithOpacity(tsl::tableBGColor));

    // App ID - use pre-formatted string
    renderer->drawString(labels[0], false, positions[0], y, SMALL_TEXT_SIZE, tsl::sectionTextColor);
    renderer->drawString(displayStrings[0], false, positions[0] + labelWidths[0] + 9, y, SMALL_TEXT_SIZE, tsl::infoTextColor);

    // Profile - use pre-formatted string
    renderer->drawString(labels[1], false, 423 - maxProfileValueWidth - labelWidths[1] - 9, y, SMALL_TEXT_SIZE, tsl::sectionTextColor);
    renderer->drawString(displayStrings[1], false, 423 - maxProfileValueWidth, y, SMALL_TEXT_SIZE, tsl::infoTextColor);

    y += 38; // Direct assignment instead of += 38

    // === MAIN DATA SECTION ===
    // renderer->drawRoundedRect(14, 106, 420, 156, 10.0f, renderer->aWithOpacity(tsl::tableBGColor));
    renderer->drawRoundedRect(14, 106, 420, 136, 12.0f, renderer->aWithOpacity(tsl::tableBGColor));
    // === FREQUENCY SECTION ===
    // Labels first (better cache locality)
    renderer->drawString(labels[2], false, positions[2], y, SMALL_TEXT_SIZE, tsl::sectionTextColor);
    renderer->drawString(labels[3], false, positions[3], y, SMALL_TEXT_SIZE, tsl::sectionTextColor);
    renderer->drawString(labels[4], false, positions[4], y, SMALL_TEXT_SIZE, tsl::sectionTextColor);

    renderer->drawString(displayStrings[5], false, dataPositions[0], y, SMALL_TEXT_SIZE, tsl::infoTextColor);  // CPU real
    renderer->drawString(displayStrings[6], false, dataPositions[1], y, SMALL_TEXT_SIZE, tsl::infoTextColor);  // GPU real
    renderer->drawString(displayStrings[7], false, dataPositions[2], y, SMALL_TEXT_SIZE, tsl::infoTextColor);  // MEM real

    // Current frequencies - use pre-formatted strings
    // renderer->drawString(displayStrings[2], false, dataPositions[0], y, SMALL_TEXT_SIZE, tsl::infoTextColor);  // CPU
    // renderer->drawString(displayStrings[3], false, dataPositions[1], y, SMALL_TEXT_SIZE, tsl::infoTextColor);  // GPU
    // renderer->drawString(displayStrings[4], false, dataPositions[2], y, SMALL_TEXT_SIZE, tsl::infoTextColor);  // MEM

    y += 20; // Direct assignment (129 + 20)

    renderer->drawString(displayStrings[19], false, positions[2], y, SMALL_TEXT_SIZE, tsl::infoTextColor);  // CPU Usage
    renderer->drawString(displayStrings[17], false, positions[3], y, SMALL_TEXT_SIZE, tsl::infoTextColor);  // GPU Usage
    if(configList.values[HorizonOCConfigValue_RAMVoltUsageDisplayMode] == RamDisplayMode_VDD2Usage || configList.values[HorizonOCConfigValue_RAMVoltUsageDisplayMode] == RamDisplayMode_VDDQUsage)
        renderer->drawString(displayStrings[18], false, positions[4], y, SMALL_TEXT_SIZE, tsl::infoTextColor);  // RAM Usage
    // === REAL FREQUENCIES ===

    // y += 20; // Direct assignment (149 + 20)

    // === VOLTAGES ===
    renderer->drawString(displayStrings[8], false, dataPositions[0], y, SMALL_TEXT_SIZE, tsl::infoTextColor);   // CPU voltage
    renderer->drawString(displayStrings[9], false, dataPositions[1], y, SMALL_TEXT_SIZE, tsl::infoTextColor);   // GPU voltage

    renderer->drawStringWithColoredSections(displayStrings[10], false, {""}, configList.values[HorizonOCConfigValue_RAMVoltUsageDisplayMode] == RamDisplayMode_VDD2VDDQ ? dataPositions[5]-16 : dataPositions[2], y, SMALL_TEXT_SIZE, tsl::infoTextColor, tsl::separatorColor);

    y += 22; // Direct assignment (169 + 22)

    // === TEMPERATURE SECTION ===
    // Labels
    renderer->drawString(labels[5], false, positions[5], y, SMALL_TEXT_SIZE, tsl::sectionTextColor);
    renderer->drawString(labels[6], false, positions[6]-1, y, SMALL_TEXT_SIZE, tsl::sectionTextColor);
    renderer->drawString(labels[7], false, positions[7], y, SMALL_TEXT_SIZE, tsl::sectionTextColor);

    // Temperatures with color - use pre-computed colors
    renderer->drawString(displayStrings[11], false, dataPositions[0], y, SMALL_TEXT_SIZE, tempColors[SysClkThermalSensor_SOC]);  // SOC
    renderer->drawString(displayStrings[12], false, dataPositions[1], y, SMALL_TEXT_SIZE, tempColors[SysClkThermalSensor_PCB]);  // PCB
    renderer->drawString(displayStrings[13], false, dataPositions[2], y, SMALL_TEXT_SIZE, tempColors[SysClkThermalSensor_Skin]);  // Skin

    y += 20; // Direct assignment (191 + 20)

    renderer->drawString(displayStrings[14], false, dataPositions[0], y, SMALL_TEXT_SIZE, tsl::infoTextColor);

    // Power labels and values
    renderer->drawString(labels[8], false, positions[8]-1, y, SMALL_TEXT_SIZE, tsl::sectionTextColor);
    renderer->drawString(labels[9], false, positions[9], y, SMALL_TEXT_SIZE, tsl::sectionTextColor);

    renderer->drawString(displayStrings[15], false, dataPositions[3], y, SMALL_TEXT_SIZE, tsl::infoTextColor);  // Power now
    renderer->drawString(displayStrings[16], false, dataPositions[4], y, SMALL_TEXT_SIZE, tsl::infoTextColor);  // Power avg

    y+=20;

    renderer->drawString(labels[10], false, positions[2], y, SMALL_TEXT_SIZE, tsl::sectionTextColor);

    renderer->drawString(displayStrings[20], false, dataPositions[0], y, SMALL_TEXT_SIZE, tempColors[HorizonOCThermalSensor_Battery]);  // Battery

    renderer->drawString(labels[13], false, positions[4], y, SMALL_TEXT_SIZE, tsl::sectionTextColor); // disp label

    renderer->drawString(displayStrings[25], false, dataPositions[2], y, SMALL_TEXT_SIZE, tsl::infoTextColor);   // disp freq

    renderer->drawString(labels[12], false, positions[3], y, SMALL_TEXT_SIZE, tsl::sectionTextColor); // fan label

    renderer->drawString(displayStrings[24], false, dataPositions[1], y, SMALL_TEXT_SIZE, tsl::infoTextColor);   // fan speed

    y+=20;

    renderer->drawString(displayStrings[21], false, dataPositions[0], y, SMALL_TEXT_SIZE, tsl::infoTextColor);   // Bat voltage
    renderer->drawString(displayStrings[23], false, positions[2] - 2, y, SMALL_TEXT_SIZE, tsl::infoTextColor);  // Bat Age

    if(this->context->isSaltyNXInstalled) {
        renderer->drawString(labels[14], false, positions[4], y, SMALL_TEXT_SIZE, tsl::sectionTextColor); // FPS label
        renderer->drawString(displayStrings[26], false, dataPositions[2], y, SMALL_TEXT_SIZE, tsl::infoTextColor);   // FPS
    }

    y+=20;
}

// Optimized refresh - now does all the string formatting once per second
void BaseMenuGui::refresh()
{
    const u64 ticks = armGetSystemTick();
    // Use cached comparison - 1 billion nanoseconds
    if (armTicksToNs(ticks - this->lastContextUpdate) <= 1000000000UL) [[likely]] {
        return; // Early exit for most calls
    }

    this->lastContextUpdate = ticks;

    // Lazy context allocation
    if (!this->context) [[unlikely]] {
        this->context = new SysClkContext;
    }

    // === SYSCLK CONTEXT UPDATE ===
    Result rc = sysclkIpcGetCurrentContext(this->context);
    if (R_FAILED(rc)) [[unlikely]] {
        FatalGui::openWithResultCode("sysclkIpcGetCurrentContext", rc);
        return;
    }

    rc = sysclkIpcGetConfigValues(&configList);
    if (R_FAILED(rc)) [[unlikely]] {
        FatalGui::openWithResultCode("sysclkIpcGetConfigValues", rc);
        return;
    }
    // dockedHighestAllowedRefreshRate = this->context->maxDisplayFreq;

    // === FORMAT ALL DISPLAY STRINGS (once per second) ===
    // App ID (hex conversion)
    sprintf(displayStrings[0], "%016lX", context->applicationId);

    // Profile
    strcpy(displayStrings[1], sysclkFormatProfile(context->profile, true));

    // Current frequencies
    u32 hz = context->freqs[SysClkModule_CPU]; // CPU
    sprintf(displayStrings[2], "%u.%u MHz", hz / 1000000U, (hz / 100000U) % 10U);

    hz = context->freqs[SysClkModule_GPU]; // GPU
    sprintf(displayStrings[3], "%u.%u MHz", hz / 1000000U, (hz / 100000U) % 10U);

    hz = context->freqs[SysClkModule_MEM]; // MEM
    sprintf(displayStrings[4], "%u.%u MHz", hz / 1000000U, (hz / 100000U) % 10U);

    // Real frequencies
    hz = context->realFreqs[SysClkModule_CPU]; // CPU
    sprintf(displayStrings[5], "%u.%u MHz", hz / 1000000U, (hz / 100000U) % 10U);

    hz = context->realFreqs[SysClkModule_GPU]; // GPU
    sprintf(displayStrings[6], "%u.%u MHz", hz / 1000000U, (hz / 100000U) % 10U);

    hz = context->realFreqs[SysClkModule_MEM]; // MEM
    sprintf(displayStrings[7], "%u.%u MHz", hz / 1000000U, (hz / 100000U) % 10U);

    // Voltages
    sprintf(displayStrings[8], "%.1f mV", context->voltages[HocClkVoltage_CPU] / 1000.0);
    sprintf(displayStrings[9], "%.1f mV", context->voltages[HocClkVoltage_GPU] / 1000.0);

    switch(configList.values[HorizonOCConfigValue_RAMVoltUsageDisplayMode]) {
        case RamDisplayMode_VDD2VDDQ:
            sprintf(displayStrings[10], "%u.%u%u mV", context->voltages[HocClkVoltage_EMCVDD2] / 1000U, (context->voltages[HocClkVoltage_EMCVDD2] % 1000U) / 100U, context->voltages[HocClkVoltage_EMCVDDQ_MarikoOnly] / 1000U);
            break;
        case RamDisplayMode_VDD2Usage:
            sprintf(displayStrings[10], "%u.%u mV", context->voltages[HocClkVoltage_EMCVDD2] / 1000U, (context->voltages[HocClkVoltage_EMCVDD2] % 1000U) / 100U);
            break;
        case RamDisplayMode_VDDQUsage:
            sprintf(displayStrings[10], "%u.%u mV", context->voltages[HocClkVoltage_EMCVDDQ_MarikoOnly] / 1000U, (context->voltages[HocClkVoltage_EMCVDDQ_MarikoOnly] % 1000U) / 100U);
            break;
        default:
            strcpy(displayStrings[10], "N/A");
            break;
    }

    // Temperatures and pre-compute colors
    u32 millis = context->temps[SysClkThermalSensor_SOC]; // SOC
    sprintf(displayStrings[11], "%u.%u °C", millis / 1000U, (millis % 1000U) / 100U);
    tempColors[SysClkThermalSensor_SOC] = tsl::GradientColor(millis * 0.001f);

    millis = context->temps[SysClkThermalSensor_PCB]; // PCB
    sprintf(displayStrings[12], "%u.%u °C", millis / 1000U, (millis % 1000U) / 100U);
    tempColors[SysClkThermalSensor_PCB] = tsl::GradientColor(millis * 0.001f);

    millis = context->temps[SysClkThermalSensor_Skin]; // Skin
    sprintf(displayStrings[13], "%u.%u °C", millis / 1000U, (millis % 1000U) / 100U);
    tempColors[SysClkThermalSensor_Skin] = tsl::GradientColor(millis * 0.001f);

    // SOC voltage (if available)
    sprintf(displayStrings[14], "%u mV", context->voltages[HocClkVoltage_SOC] / 1000U);

    // Power
    sprintf(displayStrings[15], "%d mW", context->power[0]); // Now
    sprintf(displayStrings[16], "%d mW", context->power[1]); // Avg

    sprintf(displayStrings[17], "%u%%", context->partLoad[HocClkPartLoad_GPU] / 10);
    sprintf(displayStrings[18], "%u%%", context->partLoad[SysClkPartLoad_EMC] / 10);
    sprintf(displayStrings[19], "%u%%", context->partLoad[HocClkPartLoad_CPUMax] / 10);

    millis = context->temps[HorizonOCThermalSensor_Battery]; // Battery
    sprintf(displayStrings[20], "%u.%u °C", millis / 1000U, (millis % 1000U) / 100U);
    tempColors[HorizonOCThermalSensor_Battery] = tsl::GradientColor(millis * 0.001f);

    sprintf(displayStrings[21], "%d mV", context->voltages[HocClkVoltage_Battery]); // BAT AVG

    sprintf(displayStrings[23], "%u%%", context->partLoad[HocClkPartLoad_BAT] / 1000);

    sprintf(displayStrings[24], "%u%%", context->partLoad[HocClkPartLoad_FAN]);

    sprintf(displayStrings[25], "%u Hz", context->realFreqs[HorizonOCModule_Display]);
    if(this->context->isSaltyNXInstalled) {
        if(context->fps == 254) {
            strcpy(displayStrings[26], "N/A");
        } else {
            memset(displayStrings[26], 0, sizeof(displayStrings[26]));
            sprintf(displayStrings[26], "%u", context->fps);
        }
    }
}

tsl::elm::Element* BaseMenuGui::baseUI()
{
    auto* list = new tsl::elm::List();
    list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer*, s32, s32, s32, s32) {}), 10); // add a bit of space
    this->listElement = list;
    this->listUI();

    return list;
}