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

#include "misc_gui.h"
#include "fatal_gui.h"
#include "../format.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <notification.h>
#include "labels.h"

// This workaround *may* not be nessasary, but it seems to help with reducing stutter
static void kipDataThreadFunc(void*) {
    hocClkIpcSetKipData();
}

static Thread s_kipThread;
static bool s_kipThreadPending = false;

static void sendKipData() {
    if (s_kipThreadPending) {
        threadWaitForExit(&s_kipThread);
        threadClose(&s_kipThread);
        s_kipThreadPending = false;
    }
    if (R_SUCCEEDED(threadCreate(&s_kipThread, kipDataThreadFunc, nullptr, nullptr, 0x1000, 0x2C, -2))) {
        threadStart(&s_kipThread);
        s_kipThreadPending = true;
    }
}
#if IS_MINIMAL == 1
#pragma message("Compiling with minimal features")
#endif

#define A_BTN "\ue0e0"
#define R_ARROW "\u2192"

class GeneralSettingsSubMenuGui;
class GovernorSettingsSubMenuGui;
class DisplaySubMenuGui;
class SafetySubMenuGui;
class RamSubmenuGui;
class RamTimingsSubmenuGui;
class RamLatenciesSubmenuGui;
class CpuSubmenuGui;
class GpuSubmenuGui;
class GpuCustomTableSubmenuGui;
class RamTableEditor;
class ExperimentalSettingsSubMenuGui;

MiscGui::MiscGui()
{
    this->configList = new HocClkConfigValueList {};
}

MiscGui::~MiscGui()
{
    if (s_kipThreadPending) {
        threadWaitForExit(&s_kipThread);
        threadClose(&s_kipThread);
        s_kipThreadPending = false;
    }
    delete this->configList;
    this->configToggles.clear();
    this->configTrackbars.clear();
    this->configButtons.clear();
    this->configRanges.clear();
}

void MiscGui::addConfigToggle(HocClkConfigValue configVal, const char* altName, bool kip) {
    const char* configName = altName ? altName : hocclkFormatConfigValue(configVal, true);
    tsl::elm::ToggleListItem* toggle = new tsl::elm::ToggleListItem(configName, this->configList->values[configVal]);
    if (kip)
        toggle->setTextColor(tsl::Color(255, 165, 0, 255));
    toggle->setStateChangedListener([this, configVal, kip](bool state) {
        this->configList->values[configVal] = uint64_t(state);
        Result rc = hocclkIpcSetConfigValues(this->configList);
        if (R_FAILED(rc)) {
            FatalGui::openWithResultCode("hocclkIpcSetConfigValues", rc);
        } else if (kip) {
            sendKipData();
        }
        this->lastContextUpdate = armGetSystemTick();
    });
    this->listElement->addItem(toggle);
    this->configToggles[configVal] = toggle;
}


void MiscGui::addConfigButton(HocClkConfigValue configVal,
    const char* altName,
    const ValueRange& range,
    const std::string& categoryName,
    const ValueThresholds* thresholds,
    const std::map<uint32_t, std::string>& labels,
    const std::vector<NamedValue>& namedValues,
    bool showDefaultValue,
    bool kip)
{
    const char* configName = altName ? altName : hocclkFormatConfigValue(configVal, true);

    tsl::elm::ListItem* listItem = new tsl::elm::ListItem(configName);
    if (kip)
        listItem->setTextColor(tsl::Color(255, 165, 0, 255));

    uint64_t currentValue = this->configList->values[configVal];
    char valueText[32];
    if (currentValue == 0 && showDefaultValue) {
        snprintf(valueText, sizeof(valueText), "%s", VALUE_DEFAULT_TEXT);
    } else {
        bool foundNamedValue = false;
        for (const auto& namedValue : namedValues) {
            if (currentValue == namedValue.value) {
                snprintf(valueText, sizeof(valueText), "%s", namedValue.name.c_str());
                foundNamedValue = true;
                break;
            }
        }

        if (!foundNamedValue) {
            uint64_t displayValue = currentValue / range.divisor;
            if (!range.suffix.empty()) {
                snprintf(valueText, sizeof(valueText), "%lu %s", displayValue, range.suffix.c_str());
            } else {
                snprintf(valueText, sizeof(valueText), "%lu", displayValue);
            }
        }
    }
    listItem->setValue(valueText);

    ValueThresholds thresholdsCopy = (thresholds ? *thresholds : ValueThresholds{});

    listItem->setClickListener(
        [this, configVal, range, categoryName, thresholdsCopy, labels, showDefaultValue, kip](u64 keys)
        {
            if ((keys & HidNpadButton_A) == 0)
                return false;

            std::uint32_t currentValue = this->configList->values[configVal];

            // Look up live namedValues so relabeling in refresh() is reflected
            auto nvIt = this->configNamedValues.find(configVal);
            const std::vector<NamedValue>& liveNamedValues = (nvIt != this->configNamedValues.end())
                ? nvIt->second : std::vector<NamedValue>();

            if (thresholdsCopy.warning != 0 || thresholdsCopy.danger != 0) {

                tsl::changeTo<ValueChoiceGui>(
                    currentValue,
                    range,
                    categoryName,
                    [this, configVal, kip](std::uint32_t value) {
                        this->configList->values[configVal] = value;
                        Result rc = hocclkIpcSetConfigValues(this->configList);
                        if (R_FAILED(rc)) {
                            FatalGui::openWithResultCode("hocclkIpcSetConfigValues", rc);
                            return false;
                        }
                        if (kip) {
                            sendKipData();
                        }
                        this->lastContextUpdate = armGetSystemTick();
                        return true;
                    },
                    thresholdsCopy,
                    true,
                    labels,
                    liveNamedValues,
                    showDefaultValue
                );
            } else {

                tsl::changeTo<ValueChoiceGui>(
                    currentValue,
                    range,
                    categoryName,
                    [this, configVal, kip](std::uint32_t value) {
                        this->configList->values[configVal] = value;
                        Result rc = hocclkIpcSetConfigValues(this->configList);
                        if (R_FAILED(rc)) {
                            FatalGui::openWithResultCode("hocclkIpcSetConfigValues", rc);
                            return false;
                        }
                        if (kip) {
                            sendKipData();
                        }
                        this->lastContextUpdate = armGetSystemTick();
                        return true;
                    },
                    ValueThresholds(),
                    false,
                    labels,
                    liveNamedValues,
                    showDefaultValue
                );
            }

            return true;
        });

    this->listElement->addItem(listItem);
    this->configButtons[configVal] = listItem;
    this->configRanges[configVal] = range;
    this->configNamedValues[configVal] = namedValues;
}

void MiscGui::addConfigButtonS(HocClkConfigValue configVal,
    const char* altName,
    const ValueRange& range,
    const std::string& categoryName,
    const ValueThresholds* thresholds,
    const std::map<uint32_t, std::string>& labels,
    const std::vector<NamedValue>& namedValues,
    bool showDefaultValue,
    const char* subText,
    bool kip)
{
    tsl::elm::ListItem* listItem = new tsl::elm::ListItem("");
    if (kip)
        listItem->setTextColor(tsl::Color(255, 165, 0, 255));

    uint64_t currentValue = this->configList->values[configVal];
    char valueText[32];
    if (currentValue == 0 && showDefaultValue) {
        snprintf(valueText, sizeof(valueText), "%s", VALUE_DEFAULT_TEXT);
    } else {
        bool foundNamedValue = false;
        for (const auto& namedValue : namedValues) {
            if (currentValue == namedValue.value) {
                snprintf(valueText, sizeof(valueText), "%s", namedValue.name.c_str());
                foundNamedValue = true;
                break;
            }
        }

        if (!foundNamedValue) {
            uint64_t displayValue = currentValue / range.divisor;
            if (!range.suffix.empty()) {
                snprintf(valueText, sizeof(valueText), "%lu %s", displayValue, range.suffix.c_str());
            } else {
                snprintf(valueText, sizeof(valueText), "%lu", displayValue);
            }
        }
    }

    listItem->setText(valueText);
    listItem->setValue(subText ? subText : "");

    ValueThresholds thresholdsCopy = (thresholds ? *thresholds : ValueThresholds{});

    listItem->setClickListener(
        [this, configVal, range, categoryName, thresholdsCopy, labels, showDefaultValue, kip](u64 keys)
        {
            if ((keys & HidNpadButton_A) == 0)
                return false;

            std::uint32_t currentValue = this->configList->values[configVal];

            // Look up live namedValues so relabeling in refresh() is reflected
            auto nvIt = this->configNamedValues.find(configVal);
            const std::vector<NamedValue>& liveNamedValues = (nvIt != this->configNamedValues.end())
                ? nvIt->second : std::vector<NamedValue>();

            if (thresholdsCopy.warning != 0 || thresholdsCopy.danger != 0) {

                tsl::changeTo<ValueChoiceGui>(
                    currentValue,
                    range,
                    categoryName,
                    [this, configVal, kip](std::uint32_t value) {
                        this->configList->values[configVal] = value;
                        Result rc = hocclkIpcSetConfigValues(this->configList);
                        if (R_FAILED(rc)) {
                            FatalGui::openWithResultCode("hocclkIpcSetConfigValues", rc);
                            return false;
                        }
                        if (kip) {
                            sendKipData();
                        }
                        this->lastContextUpdate = armGetSystemTick();
                        return true;
                    },
                    thresholdsCopy,
                    true,
                    labels,
                    liveNamedValues,
                    showDefaultValue
                );
            } else {

                tsl::changeTo<ValueChoiceGui>(
                    currentValue,
                    range,
                    categoryName,
                    [this, configVal, kip](std::uint32_t value) {
                        this->configList->values[configVal] = value;
                        Result rc = hocclkIpcSetConfigValues(this->configList);
                        if (R_FAILED(rc)) {
                            FatalGui::openWithResultCode("hocclkIpcSetConfigValues", rc);
                            return false;
                        }
                        if (kip) {
                            sendKipData();
                        }
                        this->lastContextUpdate = armGetSystemTick();
                        return true;
                    },
                    ValueThresholds(),
                    false,
                    labels,
                    liveNamedValues,
                    showDefaultValue
                );
            }

            return true;
        });

    this->listElement->addItem(listItem);
    this->configButtons[configVal] = listItem;
    this->configRanges[configVal] = range;
    this->configNamedValues[configVal] = namedValues;
    this->configButtonSKeys.insert(configVal);
    if (subText)
        this->configButtonSSubtext[configVal] = std::string(subText);
}

void MiscGui::updateConfigToggles() {
    for (const auto& [value, toggle] : this->configToggles) {
        if (toggle != nullptr)
            toggle->setState(this->configList->values[value]);
    }
}

void MiscGui::addFreqButton(HocClkConfigValue configVal,
                            const char* altName,
                            HocClkModule module,
                            const std::map<uint32_t, std::string>& labels)
{
    const char* configName = altName ? altName : hocclkFormatConfigValue(configVal, true);

    tsl::elm::ListItem* listItem = new tsl::elm::ListItem(configName);

    uint64_t currentMHz = this->configList->values[configVal];
    char valueText[32];
    snprintf(valueText, sizeof(valueText), "%lu MHz", currentMHz);
    listItem->setValue(valueText);

    listItem->setClickListener(
        [this, configVal, module, labels](u64 keys)
        {
            if ((keys & HidNpadButton_A) == 0)
                return false;

            std::uint32_t hzList[HOCCLK_FREQ_LIST_MAX];
            std::uint32_t hzCount;

            Result rc = hocclkIpcGetFreqList(module, hzList, HOCCLK_FREQ_LIST_MAX, &hzCount);
            if (R_FAILED(rc)) {
                FatalGui::openWithResultCode("hocclkIpcGetFreqList", rc);
                return false;
            }

            std::uint32_t currentHz = this->configList->values[configVal] * 1'000'000;

            tsl::changeTo<FreqChoiceGui>(
                currentHz,
                hzList,
                hzCount,
                module,
                [this, configVal](std::uint32_t hz)
                {
                    uint64_t mhz = hz / 1'000'000;
                    this->configList->values[configVal] = mhz;

                    Result rc = hocclkIpcSetConfigValues(this->configList);
                    if (R_FAILED(rc)) {
                        FatalGui::openWithResultCode("hocclkIpcSetConfigValues", rc);
                        return false;
                    }

                    this->lastContextUpdate = armGetSystemTick();
                    return true;
                },
                false,
                labels
            );

            return true;
        });

    this->listElement->addItem(listItem);
    this->configButtons[configVal] = listItem;

    this->configRanges[configVal] = ValueRange(0, 0, 0, "MHz", 1);
}

void MiscGui::listUI()
{
    Result rc = hocclkIpcGetConfigValues(configList);
    if (R_FAILED(rc)) [[unlikely]] {
        FatalGui::openWithResultCode("hocclkIpcGetConfigValues", rc);
        return;
    }

    ValueThresholds thresholdsDisabled(0, 0);
    std::vector<NamedValue> noNamedValues = {};

    this->listElement->addItem(new tsl::elm::CategoryHeader("Settings"));

    tsl::elm::CustomDrawer* rebootSetWarning = new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
        renderer->drawString("\uE150 Settings marked in orange", false, x + 20, y + 30, 18, tsl::style::color::ColorText);
        renderer->drawString("require a reboot to apply!", false, x + 20, y + 50, 18, tsl::style::color::ColorText);
    });
    rebootSetWarning->setBoundaries(0, 0, tsl::cfg::FramebufferWidth, 70);
    this->listElement->addItem(rebootSetWarning);

    tsl::elm::ListItem* sysmoduleSettingsSubMenu = new tsl::elm::ListItem("General Settings");
    sysmoduleSettingsSubMenu->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<GeneralSettingsSubMenuGui>();
            return true;
        }
        return false;
    });
    sysmoduleSettingsSubMenu->setValue(R_ARROW);
    this->listElement->addItem(sysmoduleSettingsSubMenu);

    tsl::elm::ListItem* governorSettingsSubMenu = new tsl::elm::ListItem("Governor Settings");
    governorSettingsSubMenu->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<GovernorSettingsSubMenuGui>();
            return true;
        }
        return false;
    });
    governorSettingsSubMenu->setValue(R_ARROW);
    this->listElement->addItem(governorSettingsSubMenu);

    tsl::elm::ListItem* safetySubmenu = new tsl::elm::ListItem("Safety Settings");
    safetySubmenu->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<SafetySubMenuGui>();
            return true;
        }
        return false;
    });
    safetySubmenu->setValue(R_ARROW);
    this->listElement->addItem(safetySubmenu);

    tsl::elm::ListItem* ramSubmenu = new tsl::elm::ListItem("RAM Settings");
    ramSubmenu->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<RamSubmenuGui>();
            return true;
        }
        return false;
    });
    ramSubmenu->setValue(R_ARROW);
    this->listElement->addItem(ramSubmenu);

    tsl::elm::ListItem* cpuSubmenu = new tsl::elm::ListItem("CPU Settings");
    cpuSubmenu->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<CpuSubmenuGui>();
            return true;
        }
        return false;
    });
    cpuSubmenu->setValue(R_ARROW);
    this->listElement->addItem(cpuSubmenu);

    tsl::elm::ListItem* gpuSubmenu = new tsl::elm::ListItem("GPU Settings");
    gpuSubmenu->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<GpuSubmenuGui>();
            return true;
        }
        return false;
    });
    gpuSubmenu->setValue(R_ARROW);
    this->listElement->addItem(gpuSubmenu);

    tsl::elm::ListItem* displaySubMenu = new tsl::elm::ListItem("Display Settings");
    displaySubMenu->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<DisplaySubMenuGui>();
            return true;
        }
        return false;
    });
    displaySubMenu->setValue(R_ARROW);
    this->listElement->addItem(displaySubMenu);

    if(this->configList->values[HocClkConfigValue_EnableExperimentalSettings]) {
        tsl::elm::ListItem* experimentalSubMenu = new tsl::elm::ListItem("Experimental Settings");
        experimentalSubMenu->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<ExperimentalSettingsSubMenuGui>();
                return true;
            }
            return false;
        });
        experimentalSubMenu->setValue(R_ARROW);
        this->listElement->addItem(experimentalSubMenu);
    }

}

class GeneralSettingsSubMenuGui : public MiscGui {
public:
    GeneralSettingsSubMenuGui() { }

protected:
    void listUI() override {
        this->listElement->addItem(new tsl::elm::CategoryHeader("General Settings"));
        ValueThresholds thresholdsDisabled(0, 0);
        std::vector<NamedValue> ramVoltDispModes = {
            NamedValue("VDD2", RamDisplayMode_VDD2),
            NamedValue("VDDQ", RamDisplayMode_VDDQ),
        };

        if(IsMariko()) {
            addConfigButton(HocClkConfigValue_RAMVoltDisplayMode, "RAM Voltage Display Mode", ValueRange(0, 12, 1, "", 0), "RAM Voltage Display Mode", &thresholdsDisabled, {}, ramVoltDispModes, false);
        }

        std::vector<NamedValue> RamDisplayUnitValues = {
            NamedValue("MHz", RamDisplayUnit_MHz),
            NamedValue("MT/s", RamDisplayUnit_MTs),
            NamedValue("MHz and MT/s", RamDisplayUnit_MHzMTs),
        };
        addConfigButton(
            HocClkConfigValue_RamDisplayUnit,
            "RAM Display Unit",
            ValueRange(0, 0, 2, "", 0),
            "RAM Display Unit",
            &thresholdsDisabled,
            {},
            RamDisplayUnitValues,
            false

        );
        addConfigButton(
            HocClkConfigValue_PollingIntervalMs,
            "Polling Interval",
            ValueRange(50, 1000, 50, "ms", 1),
            "Polling Interval",
            &thresholdsDisabled,
            {},
            {},
            false
        );

        tsl::elm::CustomDrawer* exSetWarning = new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            renderer->drawString("\uE150 Experimental Settings", false, x + 20, y + 30, 18, tsl::style::color::ColorText);
            renderer->drawString("are a work in progress! Use with caution!", false, x + 20, y + 50, 18, tsl::style::color::ColorText);
            renderer->drawString("Here be dragons!", false, x + 20, y + 70, 18, tsl::style::color::ColorText);
        });
        exSetWarning->setBoundaries(0, 0, tsl::cfg::FramebufferWidth, 90);
        this->listElement->addItem(exSetWarning);

        addConfigToggle(HocClkConfigValue_EnableExperimentalSettings, nullptr);
    }
};

class ExperimentalSettingsSubMenuGui : public MiscGui {
public:
    ExperimentalSettingsSubMenuGui() { }

protected:
    void listUI() override {
        this->listElement->addItem(new tsl::elm::CategoryHeader("Experimental Settings"));
        ValueThresholds thresholdsDisabled(0, 0);

        addConfigToggle(HocClkConfigValue_LiveCpuUv, nullptr);
        std::vector<NamedValue> gpuSchedMethodValues = {
            NamedValue("INI", GpuSchedulingOverrideMethod_Ini),
            NamedValue("NV Service", GpuSchedulingOverrideMethod_NvService),
        };
        addConfigButton(
            HocClkConfigValue_GPUSchedulingMethod,
            "GPU Scheduling Override Method",
            ValueRange(0, 0, 1, "", 0),
            "GPU Scheduling Override Method",
            &thresholdsDisabled,
            {},
            gpuSchedMethodValues,
            false
        );


        std::vector<NamedValue> ramRFMeasurementMethods = {
            NamedValue("Actmon", MemoryFrequencyMeasurementMode_Actmon),
            NamedValue("PLL", MemoryFrequencyMeasurementMode_PLL),
        };
        addConfigButton(
            HocClkConfigValue_MemoryFrequencyMeasurementMode,
            "Memory Frequency Measurement Mode",
            ValueRange(0, 0, 1, "", 0),
            "Memory Frequency Measurement Mode",
            &thresholdsDisabled,
            {},
            ramRFMeasurementMethods,
            false
        );

        tsl::elm::CustomDrawer* chargeWarningText = new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            renderer->drawString("\uE150 Overriding the charge current", false, x + 20, y + 30, 18, tsl::style::color::ColorText);
            renderer->drawString("can be dangerous and may cause", false, x + 20, y + 50, 18, tsl::style::color::ColorText);
            renderer->drawString("damage to your battery or charger!", false, x + 20, y + 70, 18, tsl::style::color::ColorText);
        });
        chargeWarningText->setBoundaries(0, 0, tsl::cfg::FramebufferWidth, 90);
        this->listElement->addItem(chargeWarningText);

        if(!IsHoag()) {
                std::vector<NamedValue> chargerCurrents = {
                    NamedValue("Disabled", 0),
                    NamedValue("1024mA", 1024),
                    NamedValue("1280mA", 1280),
                    NamedValue("1536mA", 1536),
                    NamedValue("1792mA", 1792),
                    NamedValue("2048mA", 2048),
                    NamedValue("2304mA", 2304),
                    NamedValue("2560mA", 2560),
                    NamedValue("2816mA", 2816),
                    NamedValue("3072mA", 3072),
                };

                ValueThresholds chargerThresholds(2048, 2049);

                addConfigButton(
                    HocClkConfigValue_BatteryChargeCurrent,
                    "Charge Current Override",
                    ValueRange(0, 0, 1, "", 0),
                    "Charge Current Override",
                    &chargerThresholds,
                    {},
                    chargerCurrents,
                    false
                );
        } else {
            std::vector<NamedValue> chargerCurrents = {
                NamedValue("Disabled", 0),
                NamedValue("1024mA", 1024),
                NamedValue("1280mA", 1280),
                NamedValue("1536mA", 1536),
                NamedValue("1792mA", 1792),
                NamedValue("2048mA", 2048),
                NamedValue("2304mA", 2304),
                NamedValue("2560mA", 2560),
            };

            ValueThresholds chargerThresholds(1792, 1793);

            addConfigButton(
                HocClkConfigValue_BatteryChargeCurrent,
                "Charge Current Override",
                ValueRange(0, 0, 1, "", 0),
                "Charge Current Override",
                &chargerThresholds,
                {},
                chargerCurrents,
                false
            );

        }
    }
};


class GovernorSettingsSubMenuGui : public MiscGui {
public:
    GovernorSettingsSubMenuGui() { }

protected:
    void listUI() override {
        this->listElement->addItem(new tsl::elm::CategoryHeader("Governor Settings"));
        ValueThresholds thresholdsDisabled(0, 0);

        std::vector<NamedValue> GovernorMinHz = {
            NamedValue("510 MHz", 510000000),
            NamedValue("612 MHz", 612000000),
            NamedValue("714 MHz", 714000000),
            NamedValue("816 MHz", 816000000),
            NamedValue("918 MHz", 918000000),
            NamedValue("1020 MHz", 1020000000),
        };

        addConfigButton(
            HocClkConfigValue_CpuGovernorMinimumFreq,
            "CPU Governor Minimum Frequency",
            ValueRange(0, 0, 1, "", 0),
            "CPU Governor Minimum Frequency",
            &thresholdsDisabled,
            {},
            GovernorMinHz,
            false
        );

    }
};


class DisplaySubMenuGui : public MiscGui {
public:
    DisplaySubMenuGui() { }

protected:
    void listUI() override {
        ValueThresholds thresholdsDisabled(0, 0);

        BaseMenuGui::refresh(); // get latest context
        if(!this->context)
            return;

        this->listElement->addItem(new tsl::elm::CategoryHeader("Display Settings"));
        addConfigToggle(HocClkConfigValue_OverwriteRefreshRate, nullptr);
        if(!this->context->isUsingRetroSuper) {
            tsl::elm::CustomDrawer* warningText = new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
                renderer->drawString("\uE150 Usage of unsafe display", false, x + 20, y + 30, 18, tsl::style::color::ColorText);
                renderer->drawString("refresh rates may cause stress", false, x + 20, y + 50, 18, tsl::style::color::ColorText);
                renderer->drawString("or damage to your display! ", false, x + 20, y + 70, 18, tsl::style::color::ColorText);
                renderer->drawString("Proceed at your own risk!", false, x + 20, y + 90, 18, tsl::style::color::ColorText);
            });

            warningText->setBoundaries(0, 0, tsl::cfg::FramebufferWidth, 110);
            this->listElement->addItem(warningText);
            ValueThresholds displayThresholds(60, 65);
            addConfigButton(
                HocClkConfigValue_MaxDisplayClockH,
                "Max Handheld Display",
                ValueRange(60, IsAula() ? 65 : 75, 1, " Hz", 1),
                "Display Clock",
                &displayThresholds,
                {},
                {},
                false
            );
        }
        addConfigButton(
            HocClkConfigValue_DisplayVoltage,
            "Display Voltage",
            ValueRange(800, 1200, 25, " mV", 1),
            "Display Voltage",
            &thresholdsDisabled,
            {},
            {},
            false
        );
    }
};

class SafetySubMenuGui : public MiscGui {
public:
    SafetySubMenuGui() { }

protected:
    void listUI() override {
        this->listElement->addItem(new tsl::elm::CategoryHeader("Safety Settings"));
        addConfigToggle(HocClkConfigValue_UncappedClocks, nullptr);
        addConfigToggle(HocClkConfigValue_ThermalThrottle, nullptr);
        addConfigToggle(HocClkConfigValue_HandheldTDP, nullptr);

        #if IS_MINIMAL == 0
            std::map<uint32_t, std::string> labels_pwr_l = {
                {6400, "Official Rating"}
            };

            if(IsHoag()) {
                ValueThresholds tdpThresholdsLite(6400, 7500);
                addConfigButton(
                    HocClkConfigValue_LiteTDPLimit,
                    "TDP Threshold",
                    ValueRange(4000, 8000, 100, "mW", 1),
                    "Power",
                    &tdpThresholdsLite,
                    labels_pwr_l
                );
            } else {
                ValueThresholds tdpThresholds(9600, 11000);
                addConfigButton(
                    HocClkConfigValue_HandheldTDPLimit,
                    "TDP Threshold",
                    ValueRange(8000, 12000, 100, "mW", 1),
                    "Power",
                    &tdpThresholds
                );
            }

            ValueThresholds throttleThresholds(70, 80);
            addConfigButton(
                HocClkConfigValue_ThermalThrottleThreshold,
                "Thermal Throttle Limit",
                ValueRange(50, 85, 1, "°C", 1),
                "Temp",
                &throttleThresholds
            );
        #endif
    }
};

class RamSubmenuGui : public MiscGui {
public:
    RamSubmenuGui() { }

protected:
    void listUI() override {
        ValueThresholds thresholdsDisabled(0, 0);
        std::vector<NamedValue> noNamedValues = {};

        this->listElement->addItem(new tsl::elm::CategoryHeader("RAM Settings"));

        addConfigToggle(KipConfigValue_hpMode, "HP Mode", true);

        std::map<uint32_t, std::string> emc_voltage_label = {
            {1100000, "Default (Mariko)"},
            {1125000, "Default (Erista)"},
            {1175000, "Rating"},
            {1212500, "Safe Max (Mariko)"},
            {1237500, "Safe Max (Erista)"},
            {1250000, "Unsafe Max"},
        };

        ValueThresholds vdd2Thresholds(1212500, 1250000);
        addConfigButton(
            KipConfigValue_commonEmcMemVolt,
            "RAM VDD2 Voltage",
            ValueRange(912500, 1350000, 12500, "mV", 1000, 1),
            "Voltage",
            &vdd2Thresholds,
            emc_voltage_label,
            noNamedValues,
            false,
            true
        );

        if(IsMariko()) {
            addConfigButton(
                KipConfigValue_marikoEmcVddqVolt,
                "RAM VDDQ Voltage",
                ValueRange(400000, 700000, 5000, "mV", 1000),
                "RAM VDDQ Voltage",
                &thresholdsDisabled,
                {},
                {},
                false,
                true
            );
        }

        addConfigButton(
            KipConfigValue_emcDvbShift,
            "SoC DVB Shift",
            ValueRange(0, 10, 1, "", 1),
            "SoC DVB Shift",
            &thresholdsDisabled,
            {},
            {},
            false,
            true
        );

        std::vector<NamedValue> stepMode = {
             NamedValue("66MHz", 0),
             NamedValue("100MHz", 1),
             NamedValue("Jedec", 2),
        };

        addConfigButton(KipConfigValue_stepMode, "Step Mode", ValueRange(0, 0, 2, "", 0), "Step Mode", &thresholdsDisabled, {}, stepMode, false, true);

        if (IsErista()) {
            tsl::elm::ListItem* freqSubmenu = new tsl::elm::ListItem("RAM Frequency Editor");
            freqSubmenu->setTextColor(tsl::Color(255, 165, 0, 255));
            freqSubmenu->setClickListener([](u64 keys) {
                if (keys & HidNpadButton_A) {
                    tsl::changeTo<RamTableEditor>();
                    return true;
                }
                return false;
            });
            freqSubmenu->setValue(R_ARROW);
            this->listElement->addItem(freqSubmenu);
        } else {
            RamDisplayUnit unit = (RamDisplayUnit)this->configList->values[HocClkConfigValue_RamDisplayUnit];
            std::vector<NamedValue> marikoMaxEmcClock = {
                NamedValue("1600 MHz", 1600000),
                NamedValue("1633 MHz", 1633000),
                NamedValue("1666 MHz", 1666000),
                NamedValue("1700 MHz", 1700000),
                NamedValue("1733 MHz", 1733000),
                NamedValue("1766 MHz", 1766000),
                NamedValue("1800 MHz", 1800000),
                NamedValue("1833 MHz", 1833000),
                NamedValue("1866 MHz", 1866000, "JEDEC."),
                NamedValue("1900 MHz", 1900000),
                NamedValue("1933 MHz", 1933000),
                NamedValue("1966 MHz", 1966000),
                NamedValue("1996 MHz", 1996800, "JEDEC."),
                NamedValue("2000 MHz", 2000000),
                NamedValue("2033 MHz", 2033000),
                NamedValue("2066 MHz", 2066000),
                NamedValue("2100 MHz", 2100000),
                NamedValue("2133 MHz", 2133000, "JEDEC."),
                NamedValue("2166 MHz", 2166000),
                NamedValue("2200 MHz", 2200000),
                NamedValue("2233 MHz", 2233000),
                NamedValue("2266 MHz", 2266000),
                NamedValue("2300 MHz", 2300000),
                NamedValue("2333 MHz", 2333000),
                NamedValue("2366 MHz", 2366000),
                NamedValue("2400 MHz", 2400000, "JEDEC."),
                NamedValue("2433 MHz", 2433000),
                NamedValue("2466 MHz", 2466000),
                NamedValue("2500 MHz", 2500000),
                NamedValue("2533 MHz", 2533000),
                NamedValue("2566 MHz", 2566000),
                NamedValue("2600 MHz", 2600000),
                NamedValue("2633 MHz", 2633000),
                NamedValue("2666 MHz", 2666000, "JEDEC."),
                NamedValue("2700 MHz", 2700000),
                NamedValue("2733 MHz", 2733000),
                NamedValue("2766 MHz", 2766000),
                NamedValue("2800 MHz", 2800000),
                NamedValue("2833 MHz", 2833000),
                NamedValue("2866 MHz", 2866000),
                NamedValue("2900 MHz", 2900000),
                NamedValue("2933 MHz", 2933000, "JEDEC."),
                NamedValue("2966 MHz", 2966000),
                NamedValue("3000 MHz", 3000000),
                NamedValue("3033 MHz", 3033000),
                NamedValue("3066 MHz", 3066000),
                NamedValue("3100 MHz", 3100000),
                NamedValue("3133 MHz", 3133000),
                NamedValue("3166 MHz", 3166000),
                NamedValue("3200 MHz", 3200000, "JEDEC."),
                NamedValue("3233 MHz", 3233000, "High speedo needed!"),
                NamedValue("3266 MHz", 3266000, "High speedo needed!"),
                NamedValue("3300 MHz", 3300000, "High speedo needed!"),
                // NamedValue("3333MHz (Needs extreme Speedo/PLL)", 3333000),
                // NamedValue("3366MHz (Needs extreme Speedo/PLL)", 3366000),
                // NamedValue("3400MHz (Needs extreme Speedo/PLL)", 3400000),
                // NamedValue("3433MHz (Needs ridiculous Speedo/PLL)", 3433000),
                // NamedValue("3466MHz (Needs ridiculous Speedo/PLL)", 3466000),
                // NamedValue("3500MHz (Needs ridiculous Speedo/PLL)", 3500000),
            };
            for (auto& nv : marikoMaxEmcClock)
                nv.name = formatMemClockKhzLabel(nv.value, unit);

            addConfigButton(KipConfigValue_marikoEmcMaxClock, "Ram Max Clock", ValueRange(0, 1, 1, "", 1), "Ram Max Clock", &thresholdsDisabled, {}, marikoMaxEmcClock, false, true);
        }


        tsl::elm::ListItem* latenciesSubmenu = new tsl::elm::ListItem("RAM Latency Editor");
        latenciesSubmenu->setTextColor(tsl::Color(255, 165, 0, 255));
        latenciesSubmenu->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<RamLatenciesSubmenuGui>();
                return true;
            }
            return false;
        });
        latenciesSubmenu->setValue(R_ARROW);
        this->listElement->addItem(latenciesSubmenu);

        tsl::elm::ListItem* timingsSubmenu = new tsl::elm::ListItem("RAM Timing Reductions");
        timingsSubmenu->setTextColor(tsl::Color(255, 165, 0, 255));
        timingsSubmenu->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<RamTimingsSubmenuGui>();
                return true;
            }
            return false;
        });
        timingsSubmenu->setValue(R_ARROW);
        this->listElement->addItem(timingsSubmenu);

    }
};

class RamTimingsSubmenuGui : public MiscGui {
public:
    RamTimingsSubmenuGui() { }

protected:
    void listUI() override {
        ValueThresholds thresholdsDisabled(0, 0);

        this->listElement->addItem(new tsl::elm::CategoryHeader("Memory Timings"));

        addConfigButton(KipConfigValue_t1_tRCD, "t1 tRCD", ValueRange(0, 8, 1, "", 1), "tRCD", &thresholdsDisabled, {}, {}, false, true);
        addConfigButton(KipConfigValue_t2_tRP, "t2 tRP", ValueRange(0, 8, 1, "", 1), "tRP", &thresholdsDisabled, {}, {}, false, true);
        addConfigButton(KipConfigValue_t3_tRAS, "t3 tRAS", ValueRange(0, 10, 1, "", 1), "tRAS", &thresholdsDisabled, {}, {}, false, true);
        addConfigButton(KipConfigValue_t4_tRRD, "t4 tRRD", ValueRange(0, 7, 1, "", 1), "tRRD", &thresholdsDisabled, {}, {}, false, true);
        addConfigButton(KipConfigValue_t5_tRFC, "t5 tRFC", ValueRange(0, 11, 1, "", 1), "tRFC", &thresholdsDisabled, {}, {}, false, true);
        addConfigButton(KipConfigValue_t6_tRTW, "t6 tRTW", ValueRange(0, 10, 1, "", 1), "tRTW", &thresholdsDisabled, {}, {}, false, true);
        addConfigButton(KipConfigValue_t7_tWTR, "t7 tWTR", ValueRange(0, 10, 1, "", 1), "tWTR", &thresholdsDisabled, {}, {}, false, true);
        addConfigButton(KipConfigValue_t8_tREFI, "t8 tREFI", ValueRange(0, 6, 1, "", 1), "tREFI", &thresholdsDisabled, {}, {}, false, true);

        std::vector<NamedValue> t6_tRTW_fine_tune = {
            NamedValue("-2", 0xFFFFFFFE),
            NamedValue("-1", 0xFFFFFFFF),
            NamedValue(" 0", 0),
            NamedValue("+1", 1),
            NamedValue("+2", 2),
        };

        std::vector<NamedValue> t7_tWTR_fine_tune = {
            NamedValue("-3", 0xFFFFFFFD),
            NamedValue("-2", 0xFFFFFFFE),
            NamedValue("-1", 0xFFFFFFFF),
            NamedValue(" 0", 0),
            NamedValue("+1", 1),
            NamedValue("+2", 2),
            NamedValue("+3", 3),
        };

        /* Yes this is duplicated code, yes I don't care. */
        std::vector<NamedValue> timingTbreakFreqs = {
            NamedValue("Disabled",       0),
            NamedValue("1633 MHz", 1633000),
            NamedValue("1666 MHz", 1666000),
            NamedValue("1700 MHz", 1700000),
            NamedValue("1733 MHz", 1733000),
            NamedValue("1766 MHz", 1766000),
            NamedValue("1800 MHz", 1800000),
            NamedValue("1833 MHz", 1833000),
            NamedValue("1866 MHz", 1866000, "JEDEC."),
            NamedValue("1900 MHz", 1900000),
            NamedValue("1933 MHz", 1933000),
            NamedValue("1966 MHz", 1966000),
            NamedValue("1996 MHz", 1996800, "JEDEC."),
            NamedValue("2000 MHz", 2000000),
            NamedValue("2033 MHz", 2033000),
            NamedValue("2066 MHz", 2066000),
            NamedValue("2100 MHz", 2100000),
            NamedValue("2133 MHz", 2133000, "JEDEC."),
            NamedValue("2166 MHz", 2166000),
            NamedValue("2200 MHz", 2200000),
            NamedValue("2233 MHz", 2233000),
            NamedValue("2266 MHz", 2266000),
            NamedValue("2300 MHz", 2300000),
            NamedValue("2333 MHz", 2333000),
            NamedValue("2366 MHz", 2366000),
            NamedValue("2400 MHz", 2400000, "JEDEC."),
            NamedValue("2433 MHz", 2433000),
            NamedValue("2466 MHz", 2466000),
            NamedValue("2500 MHz", 2500000),
            NamedValue("2533 MHz", 2533000),
            NamedValue("2566 MHz", 2566000),
            NamedValue("2600 MHz", 2600000),
            NamedValue("2633 MHz", 2633000),
            NamedValue("2666 MHz", 2666000, "JEDEC."),
            NamedValue("2700 MHz", 2700000),
            NamedValue("2733 MHz", 2733000),
            NamedValue("2766 MHz", 2766000),
            NamedValue("2800 MHz", 2800000),
            NamedValue("2833 MHz", 2833000),
            NamedValue("2866 MHz", 2866000),
            NamedValue("2900 MHz", 2900000),
            NamedValue("2933 MHz", 2933000, "JEDEC."),
            NamedValue("2966 MHz", 2966000),
            NamedValue("3000 MHz", 3000000),
            NamedValue("3033 MHz", 3033000),
            NamedValue("3066 MHz", 3066000),
            NamedValue("3100 MHz", 3100000),
            NamedValue("3133 MHz", 3133000),
            NamedValue("3166 MHz", 3166000),
            NamedValue("3200 MHz", 3200000, "JEDEC."),
            NamedValue("3233 MHz", 3233000, "High speedo needed"),
            NamedValue("3266 MHz", 3266000, "High speedo needed!"),
            NamedValue("3300 MHz", 3300000, "High speedo needed!"),
            // NamedValue("3333MHz (Needs extreme Speedo/PLL)", 3333000),
            // NamedValue("3366MHz (Needs extreme Speedo/PLL)", 3366000),
            // NamedValue("3400MHz (Needs extreme Speedo/PLL)", 3400000),
            // NamedValue("3433MHz (Needs ridiculous Speedo/PLL)", 3433000),
            // NamedValue("3466MHz (Needs ridiculous Speedo/PLL)", 3466000),
            // NamedValue("3500MHz (Needs ridiculous Speedo/PLL)", 3500000),
        };
        RamDisplayUnit unit = (RamDisplayUnit)this->configList->values[HocClkConfigValue_RamDisplayUnit];

        for (size_t i = 1; i < timingTbreakFreqs.size(); ++i) {
            auto &nv = timingTbreakFreqs[i];
            nv.name = formatMemClockKhzLabel(nv.value, unit);
        }

        this->listElement->addItem(new tsl::elm::CategoryHeader("Advanced"));
        addConfigButton(KipConfigValue_timingEmcTbreak, "RAM-Timing tBreak", ValueRange(0, 1, 1, "", 1), "tBreak", &thresholdsDisabled, {}, timingTbreakFreqs, false, true);
        addConfigButton(KipConfigValue_low_t6_tRTW, "Low t6 tRTW", ValueRange(0, 10, 1, "", 1), "low tRTW", &thresholdsDisabled, {}, {}, false, true );
        addConfigButton(KipConfigValue_low_t7_tWTR, "Low t7 tWTR", ValueRange(0, 10, 1, "", 1), "low tWTR", &thresholdsDisabled, {}, {}, false, true );
        addConfigButton(KipConfigValue_t2_tRP_cap, "1333WL t2 RP Cap", ValueRange(0, 8, 1, "", 1), "tRP Cap", &thresholdsDisabled, {}, {}, false, true );
        addConfigButton(KipConfigValue_t6_tRTW_fine_tune, "t6 tRTW Fine Tune", ValueRange(0, 4, 1, "", 0), "tRTW Fine Tune", &thresholdsDisabled, {}, t6_tRTW_fine_tune, false, true);
        addConfigButton(KipConfigValue_t7_tWTR_fine_tune, "t7 tWTR Fine Tune", ValueRange(0, 6, 1, "", 0), "tWTR Fine Tune", &thresholdsDisabled, {}, t7_tWTR_fine_tune, false, true);
    }
};

class RamLatenciesSubmenuGui : public MiscGui {
public:
    RamLatenciesSubmenuGui() { }

protected:

    void normalizeLatencies(const HocClkConfigValue keysArr[4]) {
        uint32_t maxClock = (uint32_t)this->configList->values[KipConfigValue_marikoEmcMaxClock];
        uint32_t vals[4];

        for (int i = 0; i < 4; i++) {
            vals[i] = (uint32_t)this->configList->values[keysArr[i]];
            if (vals[i] == 0xFFFFFFFFu) vals[i] = maxClock;
        }

        uint32_t currentLimit = 0;
        for (int i = 3; i >= 0; i--) {
            if (vals[i] != 0) {
                if (currentLimit != 0 && vals[i] > currentLimit) {
                    vals[i] = currentLimit;
                }
                currentLimit = vals[i];
            }
        }

        uint32_t last = 0;
        for (int i = 0; i < 4; i++) {
            if (vals[i] == 0) continue;

            if (vals[i] < last) vals[i] = last;
            if (vals[i] > maxClock) vals[i] = maxClock;

            last = vals[i];
        }

        for (int i = 0; i < 4; i++) {
            this->configList->values[keysArr[i]] = vals[i];
        }
    }

    void listUI() override {
        ValueThresholds thresholdsDisabled(0, 0);

        if (IsErista()) {
            std::vector<NamedValue> rlLabels = { NamedValue("1333 RL", 28), NamedValue("1600 RL", 32), NamedValue("1866 RL", 36), NamedValue("2133 RL", 40) };
            std::vector<NamedValue> wlLabels = { NamedValue("1333 WL", 12), NamedValue("1600 WL", 14), NamedValue("1866 WL", 16), NamedValue("2133 WL", 18) };

            addConfigButton(KipConfigValue_mem_burst_read_latency,  "Read Latency",  ValueRange(0, 6, 1, "", 0), "Read Latency",  &thresholdsDisabled, {}, rlLabels, false, true);
            addConfigButton(KipConfigValue_mem_burst_write_latency, "Write Latency", ValueRange(0, 6, 1, "", 0), "Write Latency", &thresholdsDisabled, {}, wlLabels, false, true);
            return;
        }

        Result rc = hocclkIpcGetConfigValues(this->configList);
        if (R_FAILED(rc)) [[unlikely]] {
            FatalGui::openWithResultCode("hocclkIpcGetConfigValues", rc);
            return;
        }

        uint32_t maxClock = (uint32_t)this->configList->values[KipConfigValue_marikoEmcMaxClock];
        RamDisplayUnit unit = (RamDisplayUnit)this->configList->values[HocClkConfigValue_RamDisplayUnit];

        static const std::vector<uint32_t> kFreqOptions = {
            1633000, 1666000, 1700000, 1733000, 1766000, 1800000,
            1833000, 1866000, 1900000, 1933000, 1966000, 1996800, 2000000,
            2033000, 2066000, 2100000, 2133000, 2166000, 2200000, 2233000,
            2266000, 2300000, 2333000, 2366000, 2400000, 2433000, 2466000,
            2500000, 2533000, 2566000, 2600000, 2633000, 2666000, 2700000,
            2733000, 2766000, 2800000, 2833000, 2866000, 2900000, 2933000,
            2966000, 3000000, 3033000, 3066000, 3100000, 3133000, 3166000,
            3200000, 3233000, 3266000, 3300000,
        };

        static const HocClkConfigValue kLatencyRKeys[4] = {
            KipConfigValue_read_latency_1333,
            KipConfigValue_read_latency_1600,
            KipConfigValue_read_latency_1866,
            KipConfigValue_read_latency_2133,
        };
        static const HocClkConfigValue kLatencyWKeys[4] = {
            KipConfigValue_write_latency_1333,
            KipConfigValue_write_latency_1600,
            KipConfigValue_write_latency_1866,
            KipConfigValue_write_latency_2133,
        };

        static const char* kTierLabels[4] = { "1333 Latency Max", "1600 Latency Max", "1866 Latency Max", "2133 Latency Max" };

        auto buildNamedValues = [&](int tierIdx) -> std::vector<NamedValue> {
            std::vector<NamedValue> nv;
            nv.push_back(NamedValue("-", 0u));
            if (tierIdx == 3) {
                nv.push_back(NamedValue(formatMemClockKhzLabel(maxClock, unit), maxClock));
                nv.push_back(NamedValue(formatMemClockKhzLabel(maxClock, unit), 0xFFFFFFFFu));
            } else {
                for (uint32_t freq : kFreqOptions) {
                    if (freq > maxClock) continue;
                    nv.push_back(NamedValue(formatMemClockKhzLabel(freq, unit), freq));
                }
                nv.push_back(NamedValue(formatMemClockKhzLabel(maxClock, unit), maxClock));
                nv.push_back(NamedValue(formatMemClockKhzLabel(maxClock, unit), 0xFFFFFFFFu));
            }
            return nv;
        };

        auto makeValueText = [&](uint32_t rawVal) -> std::string {
            if (rawVal == 0) return "-";
            if (rawVal == 0xFFFFFFFFu) return formatMemClockKhzLabel(maxClock, unit);
            return formatMemClockKhzLabel(rawVal, unit);
        };

        auto addLatencyRow = [&](const char* label, int tierIdx, const HocClkConfigValue keysArr[4]) {
            HocClkConfigValue thisKey = keysArr[tierIdx];
            uint32_t currentVal = (uint32_t)this->configList->values[thisKey];

            tsl::elm::ListItem* item = new tsl::elm::ListItem(label);
            item->setTextColor(tsl::Color(255, 165, 0, 255));
            item->setValue(makeValueText(currentVal));

            item->setClickListener([this, tierIdx, thisKey, keysArr](u64 keys) -> bool {
                if ((keys & HidNpadButton_A) == 0)
                    return false;

                uint32_t vals[4];
                for (int i = 0; i < 4; i++)
                    vals[i] = (uint32_t)this->configList->values[keysArr[i]];

                uint32_t maxClock = (uint32_t)this->configList->values[KipConfigValue_marikoEmcMaxClock];
                RamDisplayUnit unit = (RamDisplayUnit)this->configList->values[HocClkConfigValue_RamDisplayUnit];

                auto resolveVal = [maxClock](uint32_t v) -> uint32_t {
                    return (v == 0xFFFFFFFFu) ? maxClock : v;
                };

                if (tierIdx == 3) {
                    bool maxOccupied = false;
                    for (int i = 0; i < 3; i++) {
                        if (resolveVal(vals[i]) == maxClock) {
                            maxOccupied = true;
                            break;
                        }
                    }

                    std::vector<NamedValue> opts;
                    opts.push_back(NamedValue("-", 0u));

                    if (!maxOccupied) {
                        opts.push_back(NamedValue(formatMemClockKhzLabel(maxClock, unit), maxClock));
                    }

                    uint32_t displayCurrent = resolveVal(vals[3]);
                    if (maxOccupied && displayCurrent == maxClock) {
                        displayCurrent = 0;
                    }

                    tsl::changeTo<ValueChoiceGui>(
                        displayCurrent,
                        ValueRange(0, 0, 1, "", 1),
                        std::string("2133 Latency Max"),
                        [this, thisKey, keysArr](uint32_t chosen) -> bool {
                            this->configList->values[thisKey] = chosen;
                            Result rc = hocclkIpcSetConfigValues(this->configList);
                            if (R_FAILED(rc)) {
                                FatalGui::openWithResultCode("hocclkIpcSetConfigValues", rc);
                                return false;
                            }
                            sendKipData();
                            this->lastContextUpdate = armGetSystemTick();
                            return true;
                        },
                        ValueThresholds(), false,
                        std::map<uint32_t, std::string>{},
                        opts,
                        false,
                        false
                    );
                    return true;
                }

                uint32_t lowerBound = 0;
                for (int i = 0; i < tierIdx; i++) {
                    uint32_t v = resolveVal(vals[i]);
                    if (v != 0 && v > lowerBound)
                        lowerBound = v;
                }

                uint32_t upperBound = 0;
                for (int i = tierIdx + 1; i < 4; i++) {
                    uint32_t v;
                    if (i == 3) {
                        uint32_t r = resolveVal(vals[i]);
                        v = (r != 0) ? maxClock : 0;
                    } else {
                        v = resolveVal(vals[i]);
                    }
                    if (v != 0 && (upperBound == 0 || v < upperBound))
                        upperBound = v;
                }

                std::vector<NamedValue> opts;
                opts.push_back(NamedValue("-", 0u));
                for (uint32_t freq : kFreqOptions) {
                    if (freq <= lowerBound) continue;
                    if (freq > maxClock) continue;
                    if (upperBound != 0 && freq >= upperBound) continue;
                    opts.push_back(NamedValue(formatMemClockKhzLabel(freq, unit), freq));
                }

                uint32_t displayCurrent = resolveVal(vals[tierIdx]);
                bool currentInList = false;
                for (auto& nv : opts)
                    if (nv.value == displayCurrent) { currentInList = true; break; }
                if (!currentInList) displayCurrent = 0;

                tsl::changeTo<ValueChoiceGui>(
                    displayCurrent,
                    ValueRange(0, 0, 1, "", 1),
                    std::string("Latency Max"),
                    [this, thisKey, keysArr](uint32_t chosen) -> bool {
                        this->configList->values[thisKey] = chosen;
                        normalizeLatencies(keysArr);
                        Result rc = hocclkIpcSetConfigValues(this->configList);
                        if (R_FAILED(rc)) {
                            FatalGui::openWithResultCode("hocclkIpcSetConfigValues", rc);
                            return false;
                        }
                        sendKipData();
                        this->lastContextUpdate = armGetSystemTick();
                        return true;
                    },
                    ValueThresholds(), false,
                    std::map<uint32_t, std::string>{},
                    opts,
                    false,
                    false
                );
                return true;
            });

            this->listElement->addItem(item);
            this->configButtons[thisKey] = item;
            this->configRanges[thisKey]  = ValueRange(0, 0, 1, "", 1);
            this->configNamedValues[thisKey] = buildNamedValues(tierIdx);
        };

        this->listElement->addItem(new tsl::elm::CategoryHeader("Read Latency"));
        for (int i = 0; i < 4; i++)
            addLatencyRow(kTierLabels[i], i, kLatencyRKeys);

        this->listElement->addItem(new tsl::elm::CategoryHeader("Write Latency"));
        for (int i = 0; i < 4; i++)
            addLatencyRow(kTierLabels[i], i, kLatencyWKeys);
    }
};

class CpuSubmenuGui : public MiscGui {
public:
    CpuSubmenuGui() { }

protected:
    void listUI() override {
        Result rc = hocclkIpcGetConfigValues(this->configList); // populate config list early otherwise wont work
        if (R_FAILED(rc)) [[unlikely]] {
            FatalGui::openWithResultCode("hocclkIpcGetConfigValues", rc);
            return;
        }

        ValueThresholds thresholdsDisabled(0, 0);
        ValueThresholds mCpuClockThresholds(1963000, 2397000);
        ValueThresholds mCpuClockThresholdsUV(2397000, 2499000);
        ValueThresholds eCpuClockThresholds(1785000, 2091000);
        ValueThresholds eCpuClockThresholdsUV(2091000, 2193000);

        this->listElement->addItem(new tsl::elm::CategoryHeader("CPU Settings"));
        if(IsMariko()) {
            std::vector<NamedValue> ClkOptions = {
                NamedValue("1963 MHz", 1963000),
                NamedValue("2091 MHz", 2091000),
                NamedValue("2193 MHz", 2193000),
                NamedValue("2295 MHz", 2295000),
                NamedValue("2397 MHz", 2397000),
                NamedValue("2499 MHz", 2499000),
                NamedValue("2601 MHz", 2601000),
                NamedValue("2703 MHz", 2703000),
            };

            addConfigButton(
                KipConfigValue_marikoCpuBoostClock,
                "CPU Boost Clock",
                ValueRange(0, 0, 1, "", 1),
                "CPU Boost Clock",
                this->configList->values[KipConfigValue_marikoCpuUVHigh] ? &mCpuClockThresholdsUV : &mCpuClockThresholds,
                {},
                ClkOptions,
                false,
                true
            );
        } else {
            std::vector<NamedValue> ClkOptionsE = {
                NamedValue("1785 MHz", 1785000),
                NamedValue("1887 MHz", 1887000),
                NamedValue("1963 MHz", 1963000),
                NamedValue("2091 MHz", 2091000),
                NamedValue("2193 MHz", 2193000),
                NamedValue("2295 MHz", 2295000),
                NamedValue("2397 MHz", 2397000),
            };
            addConfigButton(
                KipConfigValue_eristaCpuBoostClock,
                "CPU Boost Clock",
                ValueRange(0, 0, 1, "", 1),
                "CPU Boost Clock",
                this->configList->values[KipConfigValue_eristaCpuUV] ? &eCpuClockThresholdsUV : &eCpuClockThresholds,
                {},
                ClkOptionsE,
                false,
                true
            );
        }
        if(IsErista()) {
            addConfigButton(
                KipConfigValue_eristaCpuUV,
                "CPU UV",
                ValueRange(0, 5, 1, "", 1),
                "CPU UV",
                &thresholdsDisabled,
                {},
                {},
                false,
                true
            );

            addConfigToggle(KipConfigValue_eristaCpuUnlock, "CPU Unlock", true);
            addConfigButton(
                KipConfigValue_eristaCpuVmin,
                "CPU VMIN",
                ValueRange(700, 900, 25, "mV", 1),
                "CPU VMIN",
                &thresholdsDisabled,
                {},
                {},
                false,
                true
            );

            ValueThresholds eCpuVoltThresholds(1235, 1260);
            addConfigButton(
                KipConfigValue_eristaCpuMaxVolt,
                "CPU Max Voltage",
                ValueRange(1120, 1260, 5, "mV", 1),
                "CPU Max Voltage",
                &eCpuVoltThresholds,
                {},
                {},
                false,
                true
            );

            std::vector<NamedValue> maxClkOptions = {
                NamedValue("1785 MHz", 1785),
                NamedValue("1887 MHz", 1887),
                NamedValue("1989 MHz", 1989),
                NamedValue("2091 MHz", 2091),
                NamedValue("2193 MHz", 2193),
                NamedValue("2295 MHz", 2295),
                NamedValue("2397 MHz", 2397),
            };
            ValueThresholds eCpuMaxClockThresholds(1785, 2091);
            addConfigButton(
                HocClkConfigValue_EristaMaxCpuClock,
                "CPU Max Clock",
                ValueRange(0, 0, 1, "", 1),
                "CPU Max Clock",
                &eCpuMaxClockThresholds,
                {},
                maxClkOptions,
                false
            );
        } else {
            std::vector<NamedValue> marikoTableConf = {
                // NamedValue("Auto", 0),
                NamedValue("Default", 1),
                NamedValue("1581MHz Tbreak", 2),
                NamedValue("1683MHz Tbreak", 3),
                NamedValue("Extreme UV Table", 4)
            };

            addConfigButton(
                KipConfigValue_tableConf,
                "CPU UV Table",
                ValueRange(0, 12, 1, "", 0),
                "CPU UV Table",
                &thresholdsDisabled,
                {},
                marikoTableConf,
                false,
                true
            );
            addConfigButton(
                KipConfigValue_marikoCpuUVLow,
                "CPU Low UV",
                ValueRange(0, 8, 1, "", 1),
                "CPU Low UV",
                &thresholdsDisabled,
                {},
                {},
                false,
                true
            );
            addConfigButton(
                KipConfigValue_marikoCpuUVHigh,
                "CPU High UV",
                ValueRange(0, 12, 1, "", 1),
                "CPU High UV",
                &thresholdsDisabled,
                {},
                {},
                false,
                true
            );

            std::vector<NamedValue> maxClkOptions = {
                NamedValue("1963 MHz", 1963500),
                NamedValue("2091 MHz", 2091000),
                NamedValue("2193 MHz", 2193000),
                NamedValue("2295 MHz", 2295000),
                NamedValue("2397 MHz", 2397000),
                NamedValue("2499 MHz", 2499000),
                NamedValue("2601 MHz", 2601000),
                NamedValue("2703 MHz", 2703000),
            };

            addConfigButton(
                KipConfigValue_marikoCpuMaxClock,
                "CPU Max Clock",
                ValueRange(0, 0, 1, "", 1),
                "CPU Max Clock",
                this->configList->values[KipConfigValue_marikoCpuUVHigh] ? &mCpuClockThresholdsUV : &mCpuClockThresholds,
                {},
                maxClkOptions,
                false,
                true
            );

            addConfigButton(
                KipConfigValue_marikoCpuLowVmin,
                "CPU Low VMIN",
                ValueRange(550, 750, 5, "mV", 1),
                "CPU VMIN",
                &thresholdsDisabled,
                {},
                {},
                false,
                true
            );

            addConfigButton(
                KipConfigValue_marikoCpuHighVmin,
                "CPU High VMIN",
                ValueRange(650, 900, 5, "mV", 1),
                "CPU VMIN",
                &thresholdsDisabled,
                {},
                {},
                false,
                true
            );

            ValueThresholds mCpuVoltThresholds(1160, 1180);
            addConfigButton(
                KipConfigValue_marikoCpuMaxVolt,
                "CPU Max Voltage",
                ValueRange(1000, 1200, 5, "mV", 1),
                "CPU Max Voltage",
                &mCpuVoltThresholds,
                {},
                {},
                false,
                true
            );

            addConfigToggle(HocClkConfigValue_OverwriteBoostMode, nullptr);

        }
    }
};

class RamTableEditor : public MiscGui {
public:
    RamTableEditor() { }

protected:
    void listUI() override {
        this->listElement->addItem(new tsl::elm::CategoryHeader("RAM Frequency Editor"));

        ValueThresholds thresholdsDisabled(0, 0);
        // 1600000, 1331200, 1065600, 800000, 665600, 408000, 204000
        RamDisplayUnit unit = (RamDisplayUnit)this->configList->values[HocClkConfigValue_RamDisplayUnit];

        this->listElement->addItem(new tsl::elm::ListItem(formatMemClockKhzLabel(665600, unit)));
        this->listElement->addItem(new tsl::elm::ListItem(formatMemClockKhzLabel(800000, unit)));
        this->listElement->addItem(new tsl::elm::ListItem(formatMemClockKhzLabel(1065600, unit)));
        this->listElement->addItem(new tsl::elm::ListItem(formatMemClockKhzLabel(1331200, unit)));
        this->listElement->addItem(new tsl::elm::ListItem(formatMemClockKhzLabel(1600000, unit)));

        ValueThresholds eristaRamThresholds(2208000, 2304000);

        std::vector<NamedValue> eristaMaxEmcClock = {
            NamedValue("Disabled", 1600000),
            NamedValue("1633 MHz", 1633000),
            NamedValue("1666 MHz", 1666000),
            NamedValue("1700 MHz", 1700000),
            NamedValue("1733 MHz", 1733000),
            NamedValue("1766 MHz", 1766000),
            NamedValue("1800 MHz", 1800000),
            NamedValue("1833 MHz", 1833000),
            NamedValue("1862 MHz", 1862400, "JEDEC."),
            NamedValue("1881 MHz", 1881600),
            NamedValue("1900 MHz", 1900800),
            NamedValue("1920 MHz", 1920000),
            NamedValue("1939 MHz", 1939200),
            NamedValue("1958 MHz", 1958400),
            NamedValue("1977 MHz", 1977600),
            NamedValue("1996 MHz", 1996800, "JEDEC."),
            NamedValue("2016 MHz", 2016000),
            NamedValue("2035 MHz", 2035200),
            NamedValue("2054 MHz", 2054400),
            NamedValue("2073 MHz", 2073600),
            NamedValue("2092 MHz", 2092800),
            NamedValue("2112 MHz", 2112000),
            NamedValue("2131 MHz", 2131200, "JEDEC."),
            NamedValue("2150 MHz", 2150400),
            NamedValue("2169 MHz", 2169600),
            NamedValue("2188 MHz", 2188800),
            NamedValue("2208 MHz", 2208000),
            NamedValue("2227 MHz", 2227200),
            NamedValue("2246 MHz", 2246400),
            NamedValue("2265 MHz", 2265600),
            NamedValue("2284 MHz", 2284800),
            NamedValue("2304 MHz", 2304000),
            NamedValue("2323 MHz", 2323200),
            NamedValue("2342 MHz", 2342400),
            NamedValue("2361 MHz", 2361600),
            NamedValue("2380 MHz", 2380800),
            NamedValue("2400 MHz", 2400000, "JEDEC."),
        };

        for (auto& nv : eristaMaxEmcClock)
            if (nv.name != "Disabled")
                nv.name = formatMemClockKhzLabel(nv.value, unit);

        addConfigButtonS(KipConfigValue_eristaEmcMaxClock, "", ValueRange(0, 1, 1, "", 1), "", &eristaRamThresholds, {}, eristaMaxEmcClock, false, A_BTN, true);
        addConfigButtonS(KipConfigValue_eristaEmcMaxClock1, "", ValueRange(0, 1, 1, "", 1), "", &eristaRamThresholds, {}, eristaMaxEmcClock, false, A_BTN, true);
        addConfigButtonS(KipConfigValue_eristaEmcMaxClock2, "", ValueRange(0, 1, 1, "", 1), "", &eristaRamThresholds, {}, eristaMaxEmcClock, false, A_BTN, true);
    };
};

class GpuSubmenuGui : public MiscGui {
public:
    GpuSubmenuGui() { }

protected:
    void listUI() override {
        ValueThresholds thresholdsDisabled(0, 0);
        std::vector<NamedValue> noNamedValues = {};

        this->listElement->addItem(new tsl::elm::CategoryHeader("GPU Settings"));

        std::vector<NamedValue> gpuUvConf = {
            NamedValue("HiOPT", 0),
            NamedValue("HiOPT - 15mV", 1),
            NamedValue("High UV Table", 2),
        };

        std::vector<NamedValue> mGpuVoltsVmin = {
            NamedValue("480mV", 480), NamedValue("485mV", 485), NamedValue("490mV", 490),
            NamedValue("495mV", 495), NamedValue("500mV", 500), NamedValue("505mV", 505),
            NamedValue("510mV", 510), NamedValue("515mV", 515), NamedValue("520mV", 520),
            NamedValue("525mV", 525), NamedValue("530mV", 530), NamedValue("535mV", 535),
            NamedValue("540mV", 540), NamedValue("545mV", 545), NamedValue("550mV", 550),
            NamedValue("555mV", 555), NamedValue("560mV", 560), NamedValue("565mV", 565),
            NamedValue("570mV", 570), NamedValue("575mV", 575), NamedValue("580mV", 580),
            NamedValue("585mV", 585), NamedValue("590mV", 590), NamedValue("595mV", 595),
            NamedValue("600mV", 600), NamedValue("605mV", 605), NamedValue("610mV", 610),
            NamedValue("615mV", 615), NamedValue("620mV", 620), NamedValue("625mV", 625),
            NamedValue("630mV", 630), NamedValue("635mV", 635), NamedValue("640mV", 640),
            NamedValue("645mV", 645), NamedValue("650mV", 650), NamedValue("655mV", 655),
            NamedValue("660mV", 660), NamedValue("665mV", 665), NamedValue("670mV", 670),
            NamedValue("675mV", 675), NamedValue("680mV", 680), NamedValue("685mV", 685),
            NamedValue("690mV", 690), NamedValue("695mV", 695), NamedValue("700mV", 700),
            NamedValue("705mV", 705), NamedValue("710mV", 710), NamedValue("715mV", 715),
            NamedValue("720mV", 720), NamedValue("725mV", 725), NamedValue("730mV", 730),
            NamedValue("735mV", 735), NamedValue("740mV", 740), NamedValue("745mV", 745),
            NamedValue("750mV", 750), NamedValue("755mV", 755), NamedValue("760mV", 760),
            NamedValue("765mV", 765), NamedValue("770mV", 770), NamedValue("775mV", 775),
            NamedValue("780mV", 780), NamedValue("785mV", 785), NamedValue("790mV", 790),
            NamedValue("795mV", 795)
        };

        if(IsErista()) {
            addConfigButton(
                KipConfigValue_eristaGpuUV,
                "GPU Undervolt Table",
                ValueRange(0, 1, 1, "", 1),
                "GPU Undervolt Table",
                &thresholdsDisabled,
                {},
                gpuUvConf,
                false,
                true
            );
            addConfigButton(
                KipConfigValue_eristaGpuVmin,
                "GPU Minimum Voltage",
                ValueRange(675, 875, 5, "mV", 1),
                "GPU Minimum Voltage",
                &thresholdsDisabled,
                {},
                {},
                false,
                true
            );
        } else {
            addConfigButton(
                KipConfigValue_marikoGpuUV,
                "GPU Undervolt Table",
                ValueRange(0, 1, 1, "", 1),
                "GPU Undervolt Table",
                &thresholdsDisabled,
                {},
                gpuUvConf,
                false,
                true
            );

            // tsl::elm::ListItem* vminCalcBtn = new tsl::elm::ListItem("Calculate GPU Vmin");
            // vminCalcBtn->setClickListener([this](u64 keys) {
            //     if (keys & HidNpadButton_A) {
            //         Result rc = hocClkIpcCalculateGpuVmin();
            //         if (R_FAILED(rc)) {
            //             FatalGui::openWithResultCode("hocClkIpcCalculateGpuVmin", rc);
            //             return false;
            //         }
            //         return true;
            //     }
            //     return false;
            // });

            addConfigButton(KipConfigValue_marikoGpuVmin, "GPU VMIN", ValueRange(0, 0, 0, "0", 1), "GPU VMIN", &thresholdsDisabled, {}, mGpuVoltsVmin, false, true);
            ValueThresholds MgpuVmaxThresholds(805, 850);
            addConfigButton(
                KipConfigValue_marikoGpuVmax,
                "GPU Maximum Voltage",
                ValueRange(800, 960, 5, "mV", 1),
                "GPU Maximum Voltage",
                &MgpuVmaxThresholds,
                {},
                {},
                false,
                true
            );
        }

        std::vector<NamedValue> gpuOffset = {
            NamedValue("-50 mV", 50),
            NamedValue("-45 mV", 45),
            NamedValue("-40 mV", 40),
            NamedValue("-30 mV", 30),
            NamedValue("-25 mV", 25),
            NamedValue("-20 mV", 20),
            NamedValue("-15 mV", 15),
            NamedValue("-10 mV", 10),
            NamedValue(" -5 mV", 5),
            NamedValue("Disabled", 0),
        };

        addConfigButton(
            KipConfigValue_commonGpuVoltOffset,
            "GPU Voltage Offset",
            ValueRange(0, 50, 5, "mV", 1),
            "GPU Voltage Offset",
            &thresholdsDisabled,
            {},
            gpuOffset,
            false,
            true
        );

        std::vector<NamedValue> gpuSchedValues = {
            NamedValue("Do not override", GpuSchedulingMode_DoNotOverride),
            NamedValue("Enabled (Default)", GpuSchedulingMode_Enabled, "96.6% limit"),
            NamedValue("Disabled", GpuSchedulingMode_Disabled, "99.7% limit"),
        };

        addConfigButton(
            HocClkConfigValue_GPUScheduling,
            "GPU Scheduling Override",
            ValueRange(0, 0, 1, "", 0),
            "GPU Scheduling Override",
            &thresholdsDisabled,
            {},
            gpuSchedValues,
            false
        );

        if (IsMariko()) {
            std::vector<NamedValue> dvfsOffset = {
                NamedValue("-80 mV", 0xFFFFFFB0),
                NamedValue("-75 mV", 0xFFFFFFB5),
                NamedValue("-70 mV", 0xFFFFFFBA),
                NamedValue("-65 mV", 0xFFFFFFBF),
                NamedValue("-60 mV", 0xFFFFFFC4),
                NamedValue("-55 mV", 0xFFFFFFC9),
                NamedValue("-50 mV", 0xFFFFFFCE),
                NamedValue("-45 mV", 0xFFFFFFD3),
                NamedValue("-40 mV", 0xFFFFFFD8),
                NamedValue("-30 mV", 0xFFFFFFE2),
                NamedValue("-25 mV", 0xFFFFFFE7),
                NamedValue("-20 mV", 0xFFFFFFEC),
                NamedValue("-15 mV", 0xFFFFFFF1),
                NamedValue("-10 mV", 0xFFFFFFF6),
                NamedValue(" -5 mV", 0xFFFFFFFB),
                NamedValue("Disabled",        0),
                NamedValue(" +5 mV",          5),
                NamedValue("+10 mV",         10),
                NamedValue("+15 mV",         15),
                NamedValue("+20 mV",         20),
            };

            std::vector<NamedValue> dvfsValues = {
                NamedValue("Disabled", DVFSMode_Disabled),
                NamedValue("PCV Hijack", DVFSMode_Hijack),
                // NamedValue("Official Service", DVFSMode_OfficialService),
            };

            addConfigButton(
                HocClkConfigValue_DVFSMode,
                "GPU DVFS Mode",
                ValueRange(0, 0, 1, "", 0),
                "GPU DVFS Mode",
                &thresholdsDisabled,
                {},
                dvfsValues,
                false
            );

            addConfigButton(HocClkConfigValue_DVFSOffset, "GPU DVFS Offset", ValueRange(0, 12, 1, "", 0), "GPU DVFS Offset", &thresholdsDisabled, {}, dvfsOffset, false);
        }

        tsl::elm::ListItem* customTableSubmenu = new tsl::elm::ListItem("GPU Voltage Table");
        customTableSubmenu->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<GpuCustomTableSubmenuGui>();
                return true;
            }
            return false;
        });
        customTableSubmenu->setValue(R_ARROW);
        this->listElement->addItem(customTableSubmenu);
    }
};

class GpuCustomTableSubmenuGui : public MiscGui {
public:
    GpuCustomTableSubmenuGui() { }

protected:
    void listUI() override {

        Result rc = hocclkIpcGetConfigValues(this->configList); // populate config list early otherwise wont work
        if (R_FAILED(rc)) [[unlikely]] {
            FatalGui::openWithResultCode("hocclkIpcGetConfigValues", rc);
            return;
        }

        this->listElement->addItem(new tsl::elm::CategoryHeader("GPU Custom Table (mV)"));

        ValueThresholds MgpuVmaxThresholds(800, 850);
        ValueThresholds EgpuVmaxThresholds(950, 975);

        std::vector<NamedValue> mGpuVolts = {
            NamedValue("Disabled", 2000),
            NamedValue("Auto", 0),
            NamedValue("480mV", 480), NamedValue("485mV", 485), NamedValue("490mV", 490),
            NamedValue("495mV", 495), NamedValue("500mV", 500), NamedValue("505mV", 505),
            NamedValue("510mV", 510), NamedValue("515mV", 515), NamedValue("520mV", 520),
            NamedValue("525mV", 525), NamedValue("530mV", 530), NamedValue("535mV", 535),
            NamedValue("540mV", 540), NamedValue("545mV", 545), NamedValue("550mV", 550),
            NamedValue("555mV", 555), NamedValue("560mV", 560), NamedValue("565mV", 565),
            NamedValue("570mV", 570), NamedValue("575mV", 575), NamedValue("580mV", 580),
            NamedValue("585mV", 585), NamedValue("590mV", 590), NamedValue("595mV", 595),
            NamedValue("600mV", 600), NamedValue("605mV", 605), NamedValue("610mV", 610),
            NamedValue("615mV", 615), NamedValue("620mV", 620), NamedValue("625mV", 625),
            NamedValue("630mV", 630), NamedValue("635mV", 635), NamedValue("640mV", 640),
            NamedValue("645mV", 645), NamedValue("650mV", 650), NamedValue("655mV", 655),
            NamedValue("660mV", 660), NamedValue("665mV", 665), NamedValue("670mV", 670),
            NamedValue("675mV", 675), NamedValue("680mV", 680), NamedValue("685mV", 685),
            NamedValue("690mV", 690), NamedValue("695mV", 695), NamedValue("700mV", 700),
            NamedValue("705mV", 705), NamedValue("710mV", 710), NamedValue("715mV", 715),
            NamedValue("720mV", 720), NamedValue("725mV", 725), NamedValue("730mV", 730),
            NamedValue("735mV", 735), NamedValue("740mV", 740), NamedValue("745mV", 745),
            NamedValue("750mV", 750), NamedValue("755mV", 755), NamedValue("760mV", 760),
            NamedValue("765mV", 765), NamedValue("770mV", 770), NamedValue("775mV", 775),
            NamedValue("780mV", 780), NamedValue("785mV", 785), NamedValue("790mV", 790),
            NamedValue("795mV", 795), NamedValue("800mV", 800), NamedValue("805mV", 805),
            NamedValue("810mV", 810), NamedValue("815mV", 815), NamedValue("820mV", 820),
            NamedValue("825mV", 825), NamedValue("830mV", 830), NamedValue("835mV", 835),
            NamedValue("840mV", 840), NamedValue("845mV", 845), NamedValue("850mV", 850),
            NamedValue("855mV", 855), NamedValue("860mV", 860), NamedValue("865mV", 865),
            NamedValue("870mV", 870), NamedValue("875mV", 875), NamedValue("880mV", 880),
            NamedValue("885mV", 885), NamedValue("890mV", 890), NamedValue("895mV", 895),
            NamedValue("900mV", 900), NamedValue("905mV", 905), NamedValue("910mV", 910),
            NamedValue("915mV", 915), NamedValue("920mV", 920), NamedValue("925mV", 925),
            NamedValue("930mV", 930), NamedValue("935mV", 935), NamedValue("940mV", 940),
            NamedValue("945mV", 945), NamedValue("950mV", 950), NamedValue("955mV", 955),
            NamedValue("960mV", 960),
        };

        std::vector<NamedValue> eGpuVolts = {
            NamedValue("Disabled", 2000),
            NamedValue("Auto", 0),
            NamedValue("675mV", 675), NamedValue("680mV", 680), NamedValue("685mV", 685),
            NamedValue("690mV", 690), NamedValue("695mV", 695),
            NamedValue("700mV", 700), NamedValue("705mV", 705), NamedValue("710mV", 710),
            NamedValue("715mV", 715), NamedValue("720mV", 720), NamedValue("725mV", 725),
            NamedValue("730mV", 730), NamedValue("735mV", 735), NamedValue("740mV", 740),
            NamedValue("745mV", 745), NamedValue("750mV", 750), NamedValue("755mV", 755),
            NamedValue("760mV", 760), NamedValue("765mV", 765), NamedValue("770mV", 770),
            NamedValue("775mV", 775), NamedValue("780mV", 780), NamedValue("785mV", 785),
            NamedValue("790mV", 790), NamedValue("795mV", 795), NamedValue("800mV", 800),
            NamedValue("805mV", 805), NamedValue("810mV", 810), NamedValue("815mV", 815),
            NamedValue("820mV", 820), NamedValue("825mV", 825), NamedValue("830mV", 830),
            NamedValue("835mV", 835), NamedValue("840mV", 840), NamedValue("845mV", 845),
            NamedValue("850mV", 850), NamedValue("855mV", 855), NamedValue("860mV", 860),
            NamedValue("865mV", 865), NamedValue("870mV", 870), NamedValue("875mV", 875),
            NamedValue("880mV", 880), NamedValue("885mV", 885), NamedValue("890mV", 890),
            NamedValue("895mV", 895), NamedValue("900mV", 900), NamedValue("905mV", 905),
            NamedValue("910mV", 910), NamedValue("915mV", 915), NamedValue("920mV", 920),
            NamedValue("925mV", 925), NamedValue("930mV", 930), NamedValue("935mV", 935),
            NamedValue("940mV", 940), NamedValue("945mV", 945), NamedValue("950mV", 950),
            NamedValue("955mV", 955), NamedValue("960mV", 960), NamedValue("965mV", 965),
            NamedValue("970mV", 970), NamedValue("975mV", 975), NamedValue("980mV", 980),
            NamedValue("985mV", 985), NamedValue("990mV", 990), NamedValue("995mV", 995),
        };

        std::vector<NamedValue> mGpuVolts_noAuto = {
            NamedValue("Disabled", 2000),
            NamedValue("480mV", 480), NamedValue("485mV", 485), NamedValue("490mV", 490),
            NamedValue("495mV", 495), NamedValue("500mV", 500), NamedValue("505mV", 505),
            NamedValue("510mV", 510), NamedValue("515mV", 515), NamedValue("520mV", 520),
            NamedValue("525mV", 525), NamedValue("530mV", 530), NamedValue("535mV", 535),
            NamedValue("540mV", 540), NamedValue("545mV", 545), NamedValue("550mV", 550),
            NamedValue("555mV", 555), NamedValue("560mV", 560), NamedValue("565mV", 565),
            NamedValue("570mV", 570), NamedValue("575mV", 575), NamedValue("580mV", 580),
            NamedValue("585mV", 585), NamedValue("590mV", 590), NamedValue("595mV", 595),
            NamedValue("600mV", 600), NamedValue("605mV", 605), NamedValue("610mV", 610),
            NamedValue("615mV", 615), NamedValue("620mV", 620), NamedValue("625mV", 625),
            NamedValue("630mV", 630), NamedValue("635mV", 635), NamedValue("640mV", 640),
            NamedValue("645mV", 645), NamedValue("650mV", 650), NamedValue("655mV", 655),
            NamedValue("660mV", 660), NamedValue("665mV", 665), NamedValue("670mV", 670),
            NamedValue("675mV", 675), NamedValue("680mV", 680), NamedValue("685mV", 685),
            NamedValue("690mV", 690), NamedValue("695mV", 695), NamedValue("700mV", 700),
            NamedValue("705mV", 705), NamedValue("710mV", 710), NamedValue("715mV", 715),
            NamedValue("720mV", 720), NamedValue("725mV", 725), NamedValue("730mV", 730),
            NamedValue("735mV", 735), NamedValue("740mV", 740), NamedValue("745mV", 745),
            NamedValue("750mV", 750), NamedValue("755mV", 755), NamedValue("760mV", 760),
            NamedValue("765mV", 765), NamedValue("770mV", 770), NamedValue("775mV", 775),
            NamedValue("780mV", 780), NamedValue("785mV", 785), NamedValue("790mV", 790),
            NamedValue("795mV", 795), NamedValue("800mV", 800), NamedValue("805mV", 805),
            NamedValue("810mV", 810), NamedValue("815mV", 815), NamedValue("820mV", 820),
            NamedValue("825mV", 825), NamedValue("830mV", 830), NamedValue("835mV", 835),
            NamedValue("840mV", 840), NamedValue("845mV", 845), NamedValue("850mV", 850),
            NamedValue("855mV", 855), NamedValue("860mV", 860), NamedValue("865mV", 865),
            NamedValue("870mV", 870), NamedValue("875mV", 875), NamedValue("880mV", 880),
            NamedValue("885mV", 885), NamedValue("890mV", 890), NamedValue("895mV", 895),
            NamedValue("900mV", 900), NamedValue("905mV", 905), NamedValue("910mV", 910),
            NamedValue("915mV", 915), NamedValue("920mV", 920), NamedValue("925mV", 925),
            NamedValue("930mV", 930), NamedValue("935mV", 935), NamedValue("940mV", 940),
            NamedValue("945mV", 945), NamedValue("950mV", 950), NamedValue("955mV", 955),
            NamedValue("960mV", 960),
        };

        std::vector<NamedValue> eGpuVolts_noAuto = {
            NamedValue("Disabled", 2000),
            NamedValue("700mV", 700), NamedValue("705mV", 705), NamedValue("710mV", 710),
            NamedValue("715mV", 715), NamedValue("720mV", 720), NamedValue("725mV", 725),
            NamedValue("730mV", 730), NamedValue("735mV", 735), NamedValue("740mV", 740),
            NamedValue("745mV", 745), NamedValue("750mV", 750), NamedValue("755mV", 755),
            NamedValue("760mV", 760), NamedValue("765mV", 765), NamedValue("770mV", 770),
            NamedValue("775mV", 775), NamedValue("780mV", 780), NamedValue("785mV", 785),
            NamedValue("790mV", 790), NamedValue("795mV", 795), NamedValue("800mV", 800),
            NamedValue("805mV", 805), NamedValue("810mV", 810), NamedValue("815mV", 815),
            NamedValue("820mV", 820), NamedValue("825mV", 825), NamedValue("830mV", 830),
            NamedValue("835mV", 835), NamedValue("840mV", 840), NamedValue("845mV", 845),
            NamedValue("850mV", 850), NamedValue("855mV", 855), NamedValue("860mV", 860),
            NamedValue("865mV", 865), NamedValue("870mV", 870), NamedValue("875mV", 875),
            NamedValue("880mV", 880), NamedValue("885mV", 885), NamedValue("890mV", 890),
            NamedValue("895mV", 895), NamedValue("900mV", 900), NamedValue("905mV", 905),
            NamedValue("910mV", 910), NamedValue("915mV", 915), NamedValue("920mV", 920),
            NamedValue("925mV", 925), NamedValue("930mV", 930), NamedValue("935mV", 935),
            NamedValue("940mV", 940), NamedValue("945mV", 945), NamedValue("950mV", 950),
            NamedValue("955mV", 955), NamedValue("960mV", 960), NamedValue("965mV", 965),
            NamedValue("970mV", 970), NamedValue("975mV", 975), NamedValue("980mV", 980),
            NamedValue("985mV", 985), NamedValue("990mV", 990), NamedValue("995mV", 995),
        };

        if (IsMariko()) {

            tsl::elm::CustomDrawer* warningText = new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
                renderer->drawString("\uE150 Setting GPU Clocks past", false, x + 20, y + 30, 18, tsl::style::color::ColorText);
                renderer->drawString("1075MHz without UV, 1152MHz on SLT", false, x + 20, y + 50, 18, tsl::style::color::ColorText);
                renderer->drawString("or 1228MHz on HiOPT can cause ", false, x + 20, y + 70, 18, tsl::style::color::ColorText);
                renderer->drawString("permanent damage to your Switch!", false, x + 20, y + 90, 18, tsl::style::color::ColorText);
                renderer->drawString("Proceed at your own risk!", false, x + 20, y + 110, 18, tsl::style::color::ColorText);
            });
            warningText->setBoundaries(0, 0, tsl::cfg::FramebufferWidth, 130);
            this->listElement->addItem(warningText);

            addConfigButton(KipConfigValue_g_volt_76800, "76.8MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_153600, "153.6MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_230400, "230.4MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_307200, "307.2MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_384000, "384.0MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_460800, "460.8MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_537600, "537.6MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_614400, "614.4MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_691200, "691.2MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_768000, "768.0MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_844800, "844.8MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_921600, "921.6MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_998400, "998.4MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_1075200, "1075.2MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false, true);
            if(this->configList->values[KipConfigValue_marikoGpuUV] >= GPUUVLevel_SLT)
                addConfigButton(KipConfigValue_g_volt_1152000, "1152.0MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false, true);
            if(this->configList->values[KipConfigValue_marikoGpuUV] >= GPUUVLevel_HiOPT) {
                addConfigButton(KipConfigValue_g_volt_1228800, "1228.8MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false, true);
                addConfigButton(KipConfigValue_g_volt_1267200, "1267.2MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false, true);
                addConfigButton(KipConfigValue_g_volt_1305600, "1305.6MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false, true);
                addConfigButton(KipConfigValue_g_volt_1344000, "1344.0MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts_noAuto, false, true);
                addConfigButton(KipConfigValue_g_volt_1382400, "1382.4MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts_noAuto, false, true);
                addConfigButton(KipConfigValue_g_volt_1420800, "1420.8MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts_noAuto, false, true);
                addConfigButton(KipConfigValue_g_volt_1459200, "1459.2MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts_noAuto, false, true);
                addConfigButton(KipConfigValue_g_volt_1497600, "1497.6MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts_noAuto, false, true);
                addConfigButton(KipConfigValue_g_volt_1536000, "1536.0MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts_noAuto, false, true);
            }

        } else {

            tsl::elm::CustomDrawer* warningText = new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
                renderer->drawString("\uE150 Setting GPU Clocks past", false, x + 20, y + 30, 18, tsl::style::color::ColorText);
                renderer->drawString("921MHz without UV and 960MHz on", false, x + 20, y + 50, 18, tsl::style::color::ColorText);
                renderer->drawString("SLT or HiOPT can cause ", false, x + 20, y + 70, 18, tsl::style::color::ColorText);
                renderer->drawString("permanent damage to your Switch!", false, x + 20, y + 90, 18, tsl::style::color::ColorText);
                renderer->drawString("Proceed at your own risk!", false, x + 20, y + 110, 18, tsl::style::color::ColorText);
            });
            warningText->setBoundaries(0, 0, tsl::cfg::FramebufferWidth, 130);
            this->listElement->addItem(warningText);

            addConfigButton(KipConfigValue_g_volt_e_76800, "76.8MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_e_115200, "115.2MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_e_153600, "153.6MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_e_192000, "192.0MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_e_230400, "230.4MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_e_268800, "268.8MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_e_307200, "307.2MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_e_345600, "345.6MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_e_384000, "384.0MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_e_422400, "422.4MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_e_460800, "460.8MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_e_499200, "499.2MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_e_537600, "537.6MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_e_576000, "576.0MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_e_614400, "614.4MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_e_652800, "652.8MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_e_691200, "691.2MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_e_729600, "729.6MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_e_768000, "768.0MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_e_806400, "806.4MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_e_844800, "844.8MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_e_883200, "883.2MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            addConfigButton(KipConfigValue_g_volt_e_921600, "921.6MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            if(this->configList->values[KipConfigValue_eristaGpuUV] >= GPUUVLevel_SLT)
                addConfigButton(KipConfigValue_g_volt_e_960000, "960.0MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
            if(this->configList->values[KipConfigValue_eristaGpuUV] >= GPUUVLevel_HiOPT) {
                addConfigButton(KipConfigValue_g_volt_e_998400, "998.4MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false, true);
                addConfigButton(KipConfigValue_g_volt_e_1036800, "1036.8MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts_noAuto, false, true);
                addConfigButton(KipConfigValue_g_volt_e_1075200, "1075.2MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts_noAuto, false, true);
            }
        }
    }
};


static std::string getValueDisplayText(uint64_t currentValue,
                                       const ValueRange& range,
                                       const std::vector<NamedValue>& namedValues)
{
    char valueText[32];

    for (const auto& namedValue : namedValues) {
        if (currentValue == namedValue.value) {
            return namedValue.name;
        }
    }

    if (currentValue == 0) {
        snprintf(valueText, sizeof(valueText), "%s", VALUE_DEFAULT_TEXT);
    } else {
        uint64_t displayValue = currentValue / range.divisor;
        if (!range.suffix.empty()) {
            snprintf(valueText, sizeof(valueText), "%lu %s", displayValue, range.suffix.c_str());
        } else {
            snprintf(valueText, sizeof(valueText), "%lu", displayValue);
        }
    }
    return std::string(valueText);
}

void MiscGui::refresh() {
    BaseMenuGui::refresh();

    if (this->context && ++frameCounter >= 60) {
        frameCounter = 0;

        Result rc = hocclkIpcGetConfigValues(this->configList);
        if (R_FAILED(rc)) [[unlikely]] {
            FatalGui::openWithResultCode("hocclkIpcGetConfigValues", rc);
            return;
        }
        updateConfigToggles();

        // relabel when display unit changes
        RamDisplayUnit unit = (RamDisplayUnit)this->configList->values[HocClkConfigValue_RamDisplayUnit];
        constexpr HocClkConfigValue emcKeys[] = {
            KipConfigValue_marikoEmcMaxClock,
            KipConfigValue_eristaEmcMaxClock,
            KipConfigValue_eristaEmcMaxClock1,
            KipConfigValue_eristaEmcMaxClock2,
        };
        for (auto key : emcKeys) {
            auto it = this->configNamedValues.find(key);
            if (it != this->configNamedValues.end()) {
                for (auto& nv : it->second)
                    if(nv.name != "Disabled")
                        nv.name = formatMemClockKhzLabel(nv.value, unit);
            }
        }

        for (const auto& [configVal, button] : this->configButtons) {
            uint64_t currentValue = this->configList->values[configVal];
            const ValueRange& range = this->configRanges[configVal];

            auto namedValuesIt = this->configNamedValues.find(configVal);
            const std::vector<NamedValue>& namedValues = (namedValuesIt != this->configNamedValues.end())
                ? namedValuesIt->second
                : std::vector<NamedValue>();

            char valueText[32];

            bool foundNamedValue = false;
            for (const auto& namedValue : namedValues) {
                if (currentValue == namedValue.value) {
                    snprintf(valueText, sizeof(valueText), "%s", namedValue.name.c_str());
                    foundNamedValue = true;
                    break;
                }
            }

            if (!foundNamedValue) {
                uint64_t displayValue = currentValue / range.divisor;
                if (!range.suffix.empty()) {
                    snprintf(valueText, sizeof(valueText), "%lu %s", displayValue, range.suffix.c_str());
                } else {
                    snprintf(valueText, sizeof(valueText), "%lu", displayValue);
                }
            }

            if (this->configButtonSKeys.count(configVal)) {
                button->setText(valueText);
                auto subtextIt = this->configButtonSSubtext.find(configVal);
                if (subtextIt != this->configButtonSSubtext.end())
                    button->setValue(subtextIt->second);
                else
                    button->setValue("");
            } else {
                button->setValue(valueText);
            }
        }
    }
}
