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
#if IS_MINIMAL == 1
#pragma message("Compiling with minimal features")
#endif

class RamSubmenuGui;
class RamTimingsSubmenuGui;
class RamLatenciesSubmenuGui;
class CpuSubmenuGui;
class GpuSubmenuGui;
class GpuCustomTableSubmenuGui;
class RamTableEditor;
MiscGui::MiscGui()
{
    this->configList = new SysClkConfigValueList {};
}

MiscGui::~MiscGui()
{
    delete this->configList;
    this->configToggles.clear();
    this->configTrackbars.clear();
    this->configButtons.clear();
    this->configRanges.clear();
}

void MiscGui::addConfigToggle(SysClkConfigValue configVal, const char* altName) {
    const char* configName = altName ? altName : sysclkFormatConfigValue(configVal, true);
    tsl::elm::ToggleListItem* toggle = new tsl::elm::ToggleListItem(configName, this->configList->values[configVal]);
    toggle->setStateChangedListener([this, configVal](bool state) {
        this->configList->values[configVal] = uint64_t(state);
        Result rc = sysclkIpcSetConfigValues(this->configList);
        if (R_FAILED(rc))
            FatalGui::openWithResultCode("sysclkIpcSetConfigValues", rc);
        this->lastContextUpdate = armGetSystemTick();
    });
    this->listElement->addItem(toggle);
    this->configToggles[configVal] = toggle;
}


void MiscGui::addConfigButton(SysClkConfigValue configVal,
    const char* altName,
    const ValueRange& range,
    const std::string& categoryName,
    const ValueThresholds* thresholds,
    const std::map<uint32_t, std::string>& labels,
    const std::vector<NamedValue>& namedValues,
    bool showDefaultValue)
{
    const char* configName = altName ? altName : sysclkFormatConfigValue(configVal, true);

    tsl::elm::ListItem* listItem = new tsl::elm::ListItem(configName);

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
        [this, configVal, range, categoryName, thresholdsCopy, labels, namedValues, showDefaultValue](u64 keys)
        {
            if ((keys & HidNpadButton_A) == 0)
                return false;

            std::uint32_t currentValue = this->configList->values[configVal];

            if (thresholdsCopy.warning != 0 || thresholdsCopy.danger != 0) {

                tsl::changeTo<ValueChoiceGui>(
                    currentValue,
                    range,
                    categoryName,
                    [this, configVal](std::uint32_t value) {
                        this->configList->values[configVal] = value;
                        Result rc = sysclkIpcSetConfigValues(this->configList);
                        if (R_FAILED(rc)) {
                            FatalGui::openWithResultCode("sysclkIpcSetConfigValues", rc);
                            return false;
                        }
                        this->lastContextUpdate = armGetSystemTick();
                        return true;
                    },
                    thresholdsCopy,
                    true,
                    labels,
                    namedValues,
                    showDefaultValue
                );
            } else {

                tsl::changeTo<ValueChoiceGui>(
                    currentValue,
                    range,
                    categoryName,
                    [this, configVal](std::uint32_t value) {
                        this->configList->values[configVal] = value;
                        Result rc = sysclkIpcSetConfigValues(this->configList);
                        if (R_FAILED(rc)) {
                            FatalGui::openWithResultCode("sysclkIpcSetConfigValues", rc);
                            return false;
                        }
                        this->lastContextUpdate = armGetSystemTick();
                        return true;
                    },
                    ValueThresholds(),
                    false,
                    labels,
                    namedValues,
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

void MiscGui::addConfigButtonS(SysClkConfigValue configVal,
    const char* altName,
    const ValueRange& range,
    const std::string& categoryName,
    const ValueThresholds* thresholds,
    const std::map<uint32_t, std::string>& labels,
    const std::vector<NamedValue>& namedValues,
    bool showDefaultValue,
    const char* subText)
{
    tsl::elm::ListItem* listItem = new tsl::elm::ListItem("");

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
        [this, configVal, range, categoryName, thresholdsCopy, labels, namedValues, showDefaultValue](u64 keys)
        {
            if ((keys & HidNpadButton_A) == 0)
                return false;

            std::uint32_t currentValue = this->configList->values[configVal];

            if (thresholdsCopy.warning != 0 || thresholdsCopy.danger != 0) {

                tsl::changeTo<ValueChoiceGui>(
                    currentValue,
                    range,
                    categoryName,
                    [this, configVal](std::uint32_t value) {
                        this->configList->values[configVal] = value;
                        Result rc = sysclkIpcSetConfigValues(this->configList);
                        if (R_FAILED(rc)) {
                            FatalGui::openWithResultCode("sysclkIpcSetConfigValues", rc);
                            return false;
                        }
                        this->lastContextUpdate = armGetSystemTick();
                        return true;
                    },
                    thresholdsCopy,
                    true,
                    labels,
                    namedValues,
                    showDefaultValue
                );
            } else {

                tsl::changeTo<ValueChoiceGui>(
                    currentValue,
                    range,
                    categoryName,
                    [this, configVal](std::uint32_t value) {
                        this->configList->values[configVal] = value;
                        Result rc = sysclkIpcSetConfigValues(this->configList);
                        if (R_FAILED(rc)) {
                            FatalGui::openWithResultCode("sysclkIpcSetConfigValues", rc);
                            return false;
                        }
                        this->lastContextUpdate = armGetSystemTick();
                        return true;
                    },
                    ValueThresholds(),
                    false,
                    labels,
                    namedValues,
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

void MiscGui::addFreqButton(SysClkConfigValue configVal,
                            const char* altName,
                            SysClkModule module,
                            const std::map<uint32_t, std::string>& labels)
{
    const char* configName = altName ? altName : sysclkFormatConfigValue(configVal, true);

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

            std::uint32_t hzList[SYSCLK_FREQ_LIST_MAX];
            std::uint32_t hzCount;

            Result rc = sysclkIpcGetFreqList(module, hzList, SYSCLK_FREQ_LIST_MAX, &hzCount);
            if (R_FAILED(rc)) {
                FatalGui::openWithResultCode("sysclkIpcGetFreqList", rc);
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

                    Result rc = sysclkIpcSetConfigValues(this->configList);
                    if (R_FAILED(rc)) {
                        FatalGui::openWithResultCode("sysclkIpcSetConfigValues", rc);
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
    Result rc = sysclkIpcGetConfigValues(configList);
    if (R_FAILED(rc)) [[unlikely]] {
        FatalGui::openWithResultCode("sysclkIpcGetConfigValues", rc);
        return;
    }

    this->listElement->addItem(new tsl::elm::CategoryHeader("Safety Settings"));
    addConfigToggle(HocClkConfigValue_UncappedClocks, nullptr);
    addConfigToggle(HocClkConfigValue_ThermalThrottle, nullptr);
    addConfigToggle(HocClkConfigValue_HandheldTDP, nullptr);
//  addConfigToggle(HocClkConfigValue_EnforceBoardLimit, nullptr);

    #if IS_MINIMAL == 0
        std::map<uint32_t, std::string> labels_pwr_l = {
            {6400, "Official Rating"}
        };

        if(IsHoag()) {
            ValueThresholds tdpThresholdsLite(6400, 7500);
            addConfigButton(
                HocClkConfigValue_LiteTDPLimit,
                "Lite TDP Threshold",
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

    ValueThresholds thresholdsDisabled(0, 0);
    std::vector<NamedValue> noNamedValues = {};

    this->listElement->addItem(new tsl::elm::CategoryHeader("CPU Settings"));
    addConfigToggle(HocClkConfigValue_OverwriteBoostMode, nullptr);
    std::vector<NamedValue> gpuSchedValues = {
        NamedValue("Do not override", GpuSchedulingMode_DoNotOverride),
        NamedValue("Enabled (Default)", GpuSchedulingMode_Enabled, "96.6% limit"),
        NamedValue("Disabled", GpuSchedulingMode_Disabled, "99.7% limit"),
    };

    this->listElement->addItem(new tsl::elm::CategoryHeader("GPU Settings"));
    addConfigButton(
        HorizonOCConfigValue_GPUScheduling,
        "GPU Scheduling Override",
        ValueRange(0, 0, 1, "", 0),
        "GPU Scheduling Override",
        &thresholdsDisabled,
        {},
        gpuSchedValues,
        false
    );

    if (IsMariko()) {
        std::vector<NamedValue> dvfsValues = {
            NamedValue("Disabled", DVFSMode_Disabled),
            NamedValue("PCV Hijack", DVFSMode_Hijack),
            // NamedValue("Official Service", DVFSMode_OfficialService),
            // NamedValue("Hack", DVFSMode_Hack),
        };

        addConfigButton(
            HorizonOCConfigValue_DVFSMode,
            "GPU DVFS Mode",
            ValueRange(0, 0, 1, "", 0),
            "GPU DVFS Mode",
            &thresholdsDisabled,
            {},
            dvfsValues,
            false
        );

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
            NamedValue("-10 mV", 0xFFFFFFF6),
            NamedValue(" -5 mV", 0xFFFFFFFB),
            NamedValue("Disabled",        0),
            NamedValue(" +5 mV",          5),
            NamedValue("+10 mV",         10),
            NamedValue("+15 mV",         15),
            NamedValue("+20 mV",         20),
        };

        addConfigButton(HorizonOCConfigValue_DVFSOffset, "GPU DVFS Offset", ValueRange(0, 12, 1, "", 0), "GPU DVFS Offset", &thresholdsDisabled, {}, dvfsOffset, false);
    }

    this->listElement->addItem(new tsl::elm::CategoryHeader("KIP"));

    tsl::elm::ListItem* saveBtn = new tsl::elm::ListItem("Save KIP Settings");
    saveBtn->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            Result rc = hocClkIpcSetKipData();
            if (R_FAILED(rc)) {
                FatalGui::openWithResultCode("hocClkIpcSetKipData", rc);
                return false;
            }
            return true;
        }
        return false;
    });
    this->listElement->addItem(saveBtn);

    tsl::elm::ListItem* ramSubmenu = new tsl::elm::ListItem("RAM Settings");
    ramSubmenu->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<RamSubmenuGui>();
            return true;
        }
        return false;
    });
    this->listElement->addItem(ramSubmenu);

    tsl::elm::ListItem* cpuSubmenu = new tsl::elm::ListItem("CPU Settings");
    cpuSubmenu->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<CpuSubmenuGui>();
            return true;
        }
        return false;
    });
    this->listElement->addItem(cpuSubmenu);

    tsl::elm::ListItem* gpuSubmenu = new tsl::elm::ListItem("GPU Settings");
    gpuSubmenu->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<GpuSubmenuGui>();
            return true;
        }
        return false;
    });
    this->listElement->addItem(gpuSubmenu);
    if(!IsHoag()) {
    this->listElement->addItem(new tsl::elm::CategoryHeader("Display"));
        addConfigToggle(HorizonOCConfigValue_OverwriteRefreshRate, nullptr);
        tsl::elm::CustomDrawer* warningText = new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            renderer->drawString("\uE150 Enabling unsafe display", false, x + 20, y + 30, 18, tsl::style::color::ColorText);
            renderer->drawString("refresh rates may cause stress", false, x + 20, y + 50, 18, tsl::style::color::ColorText);
            renderer->drawString("or damage to your display! ", false, x + 20, y + 70, 18, tsl::style::color::ColorText);
            renderer->drawString("Proceed at your own risk!", false, x + 20, y + 90, 18, tsl::style::color::ColorText);
        });
        warningText->setBoundaries(0, 0, tsl::cfg::FramebufferWidth, 110);
        this->listElement->addItem(warningText);
        addConfigToggle(HorizonOCConfigValue_EnableUnsafeDisplayFreqs, nullptr);
    }
    #if IS_MINIMAL == 0
        // std::vector<NamedValue> chargerCurrents = {
        //     NamedValue("Disabled", 0),
        //     NamedValue("1024mA", 1024),
        //     NamedValue("1280mA", 1280),
        //     NamedValue("1536mA", 1536),
        //     NamedValue("1792mA", 1792),
        //     NamedValue("2048mA", 2048),
        //     NamedValue("2304mA", 2304),
        //     NamedValue("2560mA", 2560),
        //     NamedValue("2816mA", 2816),
        //     NamedValue("3072mA", 3072),
        // };
        if(this->configList->values[HorizonOCConfigValue_EnableExperimentalSettings]) {
            this->listElement->addItem(new tsl::elm::CategoryHeader("Experimental"));

            addConfigToggle(HorizonOCConfigValue_LiveCpuUv, nullptr);
            std::vector<NamedValue> gpuSchedMethodValues = {
                NamedValue("INI", GpuSchedulingOverrideMethod_Ini),
                NamedValue("NV Service", GpuSchedulingOverrideMethod_NvService),
            };
            addConfigButton(
                HorizonOCConfigValue_GPUSchedulingMethod,
                "GPU Scheduling Override Method",
                ValueRange(0, 0, 1, "", 0),
                "GPU Scheduling Override Method",
                &thresholdsDisabled,
                {},
                gpuSchedMethodValues,
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
                        HorizonOCConfigValue_BatteryChargeCurrent,
                        "Charge Current Override",
                        ValueRange(0, 0, 1, "", 0),
                        "Charge Current Override",
                        &chargerThresholds,
                        {},
                        chargerCurrents,
                        false
                    );
            }
            else {
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
                    HorizonOCConfigValue_BatteryChargeCurrent,
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
    #endif
}


class RamSubmenuGui : public MiscGui {
public:
    RamSubmenuGui() { }

protected:
    void listUI() override {
        ValueThresholds thresholdsDisabled(0, 0);
        std::vector<NamedValue> noNamedValues = {};

        this->listElement->addItem(new tsl::elm::CategoryHeader("RAM Settings"));

        addConfigToggle(KipConfigValue_hpMode, "HP Mode");

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
            false
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
                false
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
            false
        );

        tsl::elm::ListItem* freqSubmenu = new tsl::elm::ListItem("RAM Frequency Editor");
        freqSubmenu->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<RamTableEditor>();
                return true;
            }
            return false;
        });
        this->listElement->addItem(freqSubmenu);

        tsl::elm::ListItem* latenciesSubmenu = new tsl::elm::ListItem("RAM Latency Editor");
        latenciesSubmenu->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<RamLatenciesSubmenuGui>();
                return true;
            }
            return false;
        });
        this->listElement->addItem(latenciesSubmenu);

        tsl::elm::ListItem* timingsSubmenu = new tsl::elm::ListItem("RAM Timing Reductions");
        timingsSubmenu->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<RamTimingsSubmenuGui>();
                return true;
            }
            return false;
        });
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

        addConfigButton(KipConfigValue_t1_tRCD, "t1 tRCD", ValueRange(0, 8, 1, "", 1), "tRCD", &thresholdsDisabled, {}, {}, false);
        addConfigButton(KipConfigValue_t2_tRP, "t2 tRP", ValueRange(0, 8, 1, "", 1), "tRP", &thresholdsDisabled, {}, {}, false);
        addConfigButton(KipConfigValue_t3_tRAS, "t3 tRAS", ValueRange(0, 10, 1, "", 1), "tRAS", &thresholdsDisabled, {}, {}, false);
        addConfigButton(KipConfigValue_t4_tRRD, "t4 tRRD", ValueRange(0, 7, 1, "", 1), "tRRD", &thresholdsDisabled, {}, {}, false);
        addConfigButton(KipConfigValue_t5_tRFC, "t5 tRFC", ValueRange(0, 11, 1, "", 1), "tRFC", &thresholdsDisabled, {}, {}, false);
        addConfigButton(KipConfigValue_t6_tRTW, "t6 tRTW", ValueRange(0, 10, 1, "", 1), "tRTW", &thresholdsDisabled, {}, {}, false);
        addConfigButton(KipConfigValue_t7_tWTR, "t7 tWTR", ValueRange(0, 10, 1, "", 1), "tWTR", &thresholdsDisabled, {}, {}, false);
        addConfigButton(KipConfigValue_t8_tREFI, "t8 tREFI", ValueRange(0, 6, 1, "", 1), "tREFI", &thresholdsDisabled, {}, {}, false);

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

        this->listElement->addItem(new tsl::elm::CategoryHeader("Advanced"));
        addConfigButton(KipConfigValue_t6_tRTW_fine_tune, "t6 tRTW Fine Tune", ValueRange(0, 4, 1, "", 0), "tRTW Fine Tune", &thresholdsDisabled, {}, t6_tRTW_fine_tune, false);
        addConfigButton(KipConfigValue_t7_tWTR_fine_tune, "t7 tWTR Fine Tune", ValueRange(0, 6, 1, "", 0), "tWTR Fine Tune", &thresholdsDisabled, {}, t7_tWTR_fine_tune, false);
    }
};

class RamLatenciesSubmenuGui : public MiscGui {
public:
    RamLatenciesSubmenuGui() { }

protected:
    void listUI() override {
        ValueThresholds thresholdsDisabled(0, 0);

        this->listElement->addItem(new tsl::elm::CategoryHeader("Memory Latencies"));

        std::vector<NamedValue> rlLabels = {
            NamedValue("1333 RL", 28),
            NamedValue("1600 RL", 32),
            NamedValue("1866 RL", 36),
            NamedValue("2133 RL", 40)
        };

        std::vector<NamedValue> wlLabels = {
            NamedValue("1333 WL", 12),
            NamedValue("1600 WL", 14),
            NamedValue("1866 WL", 16),
            NamedValue("2133 WL", 18)
        };

        addConfigButton(
            KipConfigValue_mem_burst_read_latency,
            "Read Latency",
            ValueRange(0, 6, 1, "", 0),
            "Read Latency",
            &thresholdsDisabled,
            {},
            rlLabels,
            false
        );

        addConfigButton(
            KipConfigValue_mem_burst_write_latency,
            "Write Latency",
            ValueRange(0, 6, 1, "", 0),
            "Write Latency",
            &thresholdsDisabled,
            {},
            wlLabels,
            false
        );
    }
};

class CpuSubmenuGui : public MiscGui {
public:
    CpuSubmenuGui() { }

protected:
    void listUI() override {
        ValueThresholds thresholdsDisabled(0, 0);

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
            ValueThresholds mCpuClockThresholds(1963000, 2397000);
            addConfigButton(
                KipConfigValue_marikoCpuBoostClock,
                "CPU Boost Clock",
                ValueRange(0, 0, 1, "", 1),
                "CPU Boost Clock",
                &mCpuClockThresholds,
                {},
                ClkOptions,
                false
            );
        } else {
            std::vector<NamedValue> ClkOptionsE = {
                NamedValue("1785 MHz", 1785000),
                NamedValue("1887 MHz", 1887000),
                NamedValue("1963 MHz", 1963000),
                NamedValue("2091 MHz", 2091000),
                NamedValue("2193 MHz", 2193000),
                NamedValue("2295 MHz", 2295000),
                NamedValue("2397 MHz", 2295000),
            };
            ValueThresholds eCpuClockThresholds(1785000, 2091000);
            addConfigButton(
                KipConfigValue_eristaCpuBoostClock,
                "CPU Boost Clock",
                ValueRange(0, 0, 1, "", 1),
                "CPU Boost Clock",
                &eCpuClockThresholds,
                {},
                ClkOptionsE,
                false
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
                false
            );

            addConfigToggle(KipConfigValue_eristaCpuUnlock, "CPU Unlock");
            addConfigButton(
                KipConfigValue_eristaCpuVmin,
                "CPU VMIN",
                ValueRange(700, 900, 5, "mV", 1),
                "CPU VMIN",
                &thresholdsDisabled,
                {},
                {},
                false
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
                false
            );

            std::vector<NamedValue> maxClkOptions = {
                NamedValue("1785 MHz", 1785),
                NamedValue("1887 MHz", 1887),
                NamedValue("1963 MHz", 1963),
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
                false
            );
            addConfigButton(
                KipConfigValue_marikoCpuUVLow,
                "CPU Low UV",
                ValueRange(0, 8, 1, "", 1),
                "CPU Low UV",
                &thresholdsDisabled,
                {},
                {},
                false
            );
            addConfigButton(
                KipConfigValue_marikoCpuUVHigh,
                "CPU High UV",
                ValueRange(0, 12, 1, "", 1),
                "CPU High UV",
                &thresholdsDisabled,
                {},
                {},
                false
            );

            std::vector<NamedValue> maxClkOptions = {
                NamedValue("1785 MHz", 1785000),
                NamedValue("1887 MHz", 1887000),
                NamedValue("1963 MHz", 1963000),
                NamedValue("2091 MHz", 2091000),
                NamedValue("2193 MHz", 2193000),
                NamedValue("2295 MHz", 2295000),
                NamedValue("2397 MHz", 2397000),
                NamedValue("2499 MHz", 2499000),
                NamedValue("2601 MHz", 2601000),
                NamedValue("2703 MHz", 2703000),
            };
            ValueThresholds mCpuMaxClockThresholds(1963000, 2397000);
            addConfigButton(
                KipConfigValue_marikoCpuMaxClock,
                "CPU Max Clock",
                ValueRange(0, 0, 1, "", 1),
                "CPU Max Clock",
                &mCpuMaxClockThresholds,
                {},
                maxClkOptions,
                false
            );

            addConfigButton(
                KipConfigValue_marikoCpuLowVmin,
                "CPU Low VMIN",
                ValueRange(550, 750, 5, "mV", 1),
                "CPU VMIN",
                &thresholdsDisabled,
                {},
                {},
                false
            );

            addConfigButton(
                KipConfigValue_marikoCpuHighVmin,
                "CPU High VMIN",
                ValueRange(650, 900, 5, "mV", 1),
                "CPU VMIN",
                &thresholdsDisabled,
                {},
                {},
                false
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
                false
            );
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
        if(IsMariko()) {
            tsl::elm::ListItem* ramItem1600 = new tsl::elm::ListItem("1600 MHz");
            this->listElement->addItem(ramItem1600);

            std::vector<NamedValue> marikoMaxEmcClock = {
                NamedValue("Disabled", 1600000),
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
            addConfigButtonS(
                KipConfigValue_marikoEmcMaxClock,
                "",
                ValueRange(0, 1, 1, "", 1),
                "",
                &thresholdsDisabled,
                {},
                marikoMaxEmcClock,
                false,
                "\ue0e0"
            );
        } else {
            // 1600000, 1331200, 1065600, 800000, 665600, 408000, 204000

            tsl::elm::ListItem* ramItem665 = new tsl::elm::ListItem("665 MHz");
            this->listElement->addItem(ramItem665);

            tsl::elm::ListItem* ramItem800 = new tsl::elm::ListItem("800 MHz");
            this->listElement->addItem(ramItem800);

            tsl::elm::ListItem* ramItem1065 = new tsl::elm::ListItem("1065 MHz");
            this->listElement->addItem(ramItem1065);

            tsl::elm::ListItem* ramItem1331 = new tsl::elm::ListItem("1331 MHz");
            this->listElement->addItem(ramItem1331);

            tsl::elm::ListItem* ramItem1600 = new tsl::elm::ListItem("1600 MHz");
            this->listElement->addItem(ramItem1600);

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

            addConfigButtonS(KipConfigValue_eristaEmcMaxClock, "", ValueRange(0, 1, 1, "", 1), "", &eristaRamThresholds, {}, eristaMaxEmcClock, false, "\ue0e0");
            addConfigButtonS(KipConfigValue_eristaEmcMaxClock1, "", ValueRange(0, 1, 1, "", 1), "", &eristaRamThresholds, {}, eristaMaxEmcClock, false, "\ue0e0");
            addConfigButtonS(KipConfigValue_eristaEmcMaxClock2, "", ValueRange(0, 1, 1, "", 1), "", &eristaRamThresholds, {}, eristaMaxEmcClock, false, "\ue0e0");
        }
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
            NamedValue("No Undervolt", 0),
            NamedValue("SLT Table", 1),
            NamedValue("HiOPT Table", 2),
        };

        std::vector<NamedValue> mGpuVoltsVmin = {
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
            NamedValue("795mV", 795), NamedValue("800mV", 800)
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
                false
            );
            addConfigButton(
                KipConfigValue_eristaGpuVmin,
                "GPU Minimum Voltage",
                ValueRange(700, 875, 5, "mV", 1),
                "GPU Minimum Voltage",
                &thresholdsDisabled,
                {},
                {},
                false
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
                false
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

            addConfigButton(KipConfigValue_marikoGpuVmin, "GPU VMIN", ValueRange(0, 0, 0, "0", 1), "GPU VMIN", &thresholdsDisabled, {}, mGpuVoltsVmin, false);
            ValueThresholds MgpuVmaxThresholds(800, 850);
            addConfigButton(
                KipConfigValue_marikoGpuVmax,
                "GPU Maximum Voltage",
                ValueRange(750, 960, 5, "mV", 1),
                "GPU Maximum Voltage",
                &MgpuVmaxThresholds,
                {},
                {},
                false
            );
        }

        addConfigButton(
            KipConfigValue_commonGpuVoltOffset,
            "GPU Voltage Offset",
            ValueRange(0, 50, 5, "mV", 1),
            "GPU Voltage Offset",
            &thresholdsDisabled,
            {},
            {},
            false
        );

        tsl::elm::ListItem* customTableSubmenu = new tsl::elm::ListItem("GPU Voltage Table");
        customTableSubmenu->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<GpuCustomTableSubmenuGui>();
                return true;
            }
            return false;
        });
        this->listElement->addItem(customTableSubmenu);
    }
};

class GpuCustomTableSubmenuGui : public MiscGui {
public:
    GpuCustomTableSubmenuGui() { }

protected:
    void listUI() override {

        Result rc = sysclkIpcGetConfigValues(this->configList); // populate config list early otherwise wont work
        if (R_FAILED(rc)) [[unlikely]] {
            FatalGui::openWithResultCode("sysclkIpcGetConfigValues", rc);
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

            addConfigButton(KipConfigValue_g_volt_76800, "76.8MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_153600, "153.6MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_230400, "230.4MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_307200, "307.2MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_384000, "384.0MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_460800, "460.8MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_537600, "537.6MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_614400, "614.4MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_691200, "691.2MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_768000, "768.0MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_844800, "844.8MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_921600, "921.6MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_998400, "998.4MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_1075200, "1075.2MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false);
            if(this->configList->values[KipConfigValue_marikoGpuUV] >= GPUUVLevel_SLT)
                addConfigButton(KipConfigValue_g_volt_1152000, "1152.0MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false);
            if(this->configList->values[KipConfigValue_marikoGpuUV] >= GPUUVLevel_HiOPT) {
                addConfigButton(KipConfigValue_g_volt_1228800, "1228.8MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false);
                addConfigButton(KipConfigValue_g_volt_1267200, "1267.2MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false);
                addConfigButton(KipConfigValue_g_volt_1305600, "1305.6MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts, false);
                addConfigButton(KipConfigValue_g_volt_1344000, "1344.0MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts_noAuto, false);
                addConfigButton(KipConfigValue_g_volt_1382400, "1382.4MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts_noAuto, false);
                addConfigButton(KipConfigValue_g_volt_1420800, "1420.8MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts_noAuto, false);
                addConfigButton(KipConfigValue_g_volt_1459200, "1459.2MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts_noAuto, false);
                addConfigButton(KipConfigValue_g_volt_1497600, "1497.6MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts_noAuto, false);
                addConfigButton(KipConfigValue_g_volt_1536000, "1536.0MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &MgpuVmaxThresholds, {}, mGpuVolts_noAuto, false);
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

            addConfigButton(KipConfigValue_g_volt_e_76800, "76.8MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_e_115200, "115.2MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_e_153600, "153.6MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_e_192000, "192.0MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_e_230400, "230.4MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_e_268800, "268.8MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_e_307200, "307.2MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_e_345600, "345.6MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_e_384000, "384.0MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_e_422400, "422.4MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_e_460800, "460.8MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_e_499200, "499.2MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_e_537600, "537.6MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_e_576000, "576.0MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_e_614400, "614.4MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_e_652800, "652.8MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_e_691200, "691.2MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_e_729600, "729.6MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_e_768000, "768.0MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_e_806400, "806.4MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_e_844800, "844.8MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_e_883200, "883.2MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            addConfigButton(KipConfigValue_g_volt_e_921600, "921.6MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            if(this->configList->values[KipConfigValue_eristaGpuUV] >= GPUUVLevel_SLT)
                addConfigButton(KipConfigValue_g_volt_e_960000, "960.0MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
            if(this->configList->values[KipConfigValue_eristaGpuUV] >= GPUUVLevel_HiOPT) {
                addConfigButton(KipConfigValue_g_volt_e_998400, "998.4MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts, false);
                addConfigButton(KipConfigValue_g_volt_e_1036800, "1036.8MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts_noAuto, false);
                addConfigButton(KipConfigValue_g_volt_e_1075200, "1075.2MHz", ValueRange(0, 0, 0, "0", 1), "Voltage", &EgpuVmaxThresholds, {}, eGpuVolts_noAuto, false);
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

        Result rc = sysclkIpcGetConfigValues(this->configList);
        if (R_FAILED(rc)) [[unlikely]] {
            FatalGui::openWithResultCode("sysclkIpcGetConfigValues", rc);
            return;
        }
        updateConfigToggles();

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
