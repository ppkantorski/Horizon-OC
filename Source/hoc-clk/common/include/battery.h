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

#pragma once
#include <switch.h>
#include <inttypes.h>
#include <string.h>
typedef enum {
    BatteryFlag_NoHub  = BIT(0),  // Hub is disconnected
    BatteryFlag_Rail   = BIT(8),  // At least one Joy-con is charging from rail
    BatteryFlag_SPDSRC = BIT(12), // OTG
    BatteryFlag_ACC    = BIT(16)  // Accessory
} BatteryChargeFlags;

typedef enum {
    PDState_NewPDO      = 1, // Received new Power Data Object
    PDState_NoPD        = 2, // No Power Delivery source is detected
    PDState_AcceptedRDO = 3  // Received and accepted Request Data Object
} BatteryPDControllerState;

// Charger type detection
typedef enum {
    ChargerType_None         = 0,
    ChargerType_PD           = 1,
    ChargerType_TypeC_1500mA = 2,
    ChargerType_TypeC_3000mA = 3,
    ChargerType_DCP          = 4, // Dedicated Charging Port
    ChargerType_CDP          = 5, // Charging Downstream Port
    ChargerType_SDP          = 6, // Standard Downstream Port
    ChargerType_Apple_500mA  = 7,
    ChargerType_Apple_1000mA = 8,
    ChargerType_Apple_2000mA = 9
} BatteryChargerType;
typedef enum {
    PowerRole_Sink   = 1, // Device is receiving power
    PowerRole_Source = 2  // Device is providing power
} BatteryPowerRole;

typedef struct {
    int32_t InputCurrentLimit;       // Input (Sink) current limit in mA
    int32_t VBUSCurrentLimit;        // Output (Source/VBUS/OTG) current limit in mA
    int32_t ChargeCurrentLimit;      // Battery charging current limit in mA
    int32_t ChargeVoltageLimit;      // Battery charging voltage limit in mV
    int32_t unk_x10;                 // Unknown field (possibly enum)
    int32_t unk_x14;                 // Unknown field (possibly flags)
    BatteryPDControllerState PDControllerState; // PD Controller State
    int32_t BatteryTemperature;      // Battery temperature in milli-Celsius
    int32_t RawBatteryCharge;        // Battery charge in percentmille
    int32_t VoltageAvg;              // Average voltage in mV
    int32_t BatteryAge;              // Battery health (capacity full/design) in pcm
    BatteryPowerRole PowerRole;      // Current power role
    BatteryChargerType ChargerType;  // Type of charger connected
    int32_t ChargerVoltageLimit;     // Charger voltage limit in mV
    int32_t ChargerCurrentLimit;     // Charger current limit in mA
    BatteryChargeFlags Flags;        // Various status flags
} BatteryChargeInfo;

#define IS_BATTERY_CHARGING_ENABLED(info) (((info)->unk_x14 >> 8) & 1)

static inline int batteryInfoGetTemperatureMiliCelsius(BatteryChargeInfo *info) {
    return info->BatteryTemperature;
}

static inline float batteryInfoGetChargePercent(BatteryChargeInfo *info) {
    return (float)info->RawBatteryCharge / 1000.0f;
}

static inline float batteryInfoGetBatteryHealthPercent(BatteryChargeInfo *info) {
    return (float)info->BatteryAge / 1000.0f;
}

static inline bool batteryInfoIsCharging(BatteryChargeInfo *info) {
    return IS_BATTERY_CHARGING_ENABLED(info);
}

Result batteryInfoInitialize(void);
void batteryInfoExit(void);
Result batteryInfoGetChargeInfo(BatteryChargeInfo *out);
Result batteryInfoGetChargePercentage(u32 *out);
Result batteryInfoIsEnoughPowerSupplied(bool *out);
Result batteryInfoEnableCharging(void);
Result batteryInfoDisableCharging(void);
Result batteryInfoEnableFastCharging(void);
Result batteryInfoDisableFastCharging(void);
const char* batteryInfoGetChargerTypeString(BatteryChargerType type);
const char* batteryInfoGetPowerRoleString(BatteryPowerRole role);
const char* batteryInfoGetPDStateString(BatteryPDControllerState state);
