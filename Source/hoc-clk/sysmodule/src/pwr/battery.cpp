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

#include <switch.h>
#include <cstring>
#include "battery.h"

// Internal PSM service handle
static Service g_psmService = {0};
static bool g_batteryInfoInitialized = false;

static const char* s_chargerTypeStrings[] = {
    "None",
    "Power Delivery",
    "USB-C @ 1.5A",
    "USB-C @ 3.0A",
    "USB-DCP",
    "USB-CDP",
    "USB-SDP",
    "Apple @ 0.5A",
    "Apple @ 1.0A",
    "Apple @ 2.0A",
};

static const char* s_powerRoleStrings[] = {
    "Unknown",
    "Sink",
    "Source",
};

static const char* s_pdStateStrings[] = {
    "Unknown",
    "New PDO Received",
    "No PD Source",
    "RDO Accepted"
};

// Internal PSM command implementations
static Result psmGetBatteryChargeInfoFields(BatteryChargeInfo *out) {
    if (!g_batteryInfoInitialized)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    return serviceDispatchOut(&g_psmService, 17, *out);
}

static Result psmEnableBatteryCharging_internal(void) {
    if (!g_batteryInfoInitialized)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    return serviceDispatch(&g_psmService, 2);
}

static Result psmDisableBatteryCharging_internal(void) {
    if (!g_batteryInfoInitialized)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    return serviceDispatch(&g_psmService, 3);
}

static Result psmEnableFastBatteryCharging_internal(void) {
    if (!g_batteryInfoInitialized)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    return serviceDispatch(&g_psmService, 10);
}

static Result psmDisableFastBatteryCharging_internal(void) {
    if (!g_batteryInfoInitialized)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    return serviceDispatch(&g_psmService, 11);
}

Result batteryInfoInitialize(void) {
    if (g_batteryInfoInitialized)
        return 0;

    Result rc = psmInitialize();
    if (R_SUCCEEDED(rc)) {
        memcpy(&g_psmService, psmGetServiceSession(), sizeof(Service));
        g_batteryInfoInitialized = true;
    }

    return rc;
}

void batteryInfoExit(void) {
    if (g_batteryInfoInitialized) {
        psmExit();
        memset(&g_psmService, 0, sizeof(Service));
        g_batteryInfoInitialized = false;
    }
}

Result batteryInfoGetChargeInfo(BatteryChargeInfo *out) {
    if (!out)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    return psmGetBatteryChargeInfoFields(out);
}

Result batteryInfoGetChargePercentage(u32 *out) {
    if (!g_batteryInfoInitialized)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    return psmGetBatteryChargePercentage(out);
}

Result batteryInfoIsEnoughPowerSupplied(bool *out) {
    if (!g_batteryInfoInitialized)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    return psmIsEnoughPowerSupplied(out);
}

Result batteryInfoEnableCharging(void) {
    return psmEnableBatteryCharging_internal();
}

Result batteryInfoDisableCharging(void) {
    return psmDisableBatteryCharging_internal();
}

Result batteryInfoEnableFastCharging(void) {
    return psmEnableFastBatteryCharging_internal();
}

Result batteryInfoDisableFastCharging(void) {
    return psmDisableFastBatteryCharging_internal();
}

const char* batteryInfoGetChargerTypeString(BatteryChargerType type) {
    if (type < 0 || type > ChargerType_Apple_2000mA)
        return "Unknown";

    return s_chargerTypeStrings[type];
}

const char* batteryInfoGetPowerRoleString(BatteryPowerRole role) {
    if (role < PowerRole_Sink || role > PowerRole_Source)
        return s_powerRoleStrings[0];

    return s_powerRoleStrings[role];
}

const char* batteryInfoGetPDStateString(BatteryPDControllerState state) {
    if (state < PDState_NewPDO || state > PDState_AcceptedRDO)
        return s_pdStateStrings[0];

    return s_pdStateStrings[state];
}