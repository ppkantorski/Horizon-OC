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

#include <switch.h>
#include <hocclk.h>
#include <nxExt.h>
#include "board.hpp"
#include "../config.hpp"
#include "../integrations.hpp"

namespace board {

    HocClkProfile GetProfile() {
        u32 mode = 0;
        Result rc = apmExtGetPerformanceMode(&mode);
        ASSERT_RESULT_OK(rc, "apmExtGetPerformanceMode");

        // ReverseNX sync: if enabled, let SaltyNX's displaySync field override
        // the physical dock state.  Bit 0 = forced handheld, bit 1 = forced docked.
        // Only override when the bit is actually set; fall through to normal
        // detection when neither bit is set (ReverseNX not active for this title).
        if (config::GetConfigValue(HocClkConfigValue_ReverseNXSync)) {
            u8 ds = integrations::GetDisplaySync();
            if (ds & 0x02) {
                // ReverseNX forcing docked — treat as docked regardless of hardware
                return HocClkProfile_Docked;
            } else if (ds & 0x01) {
                // ReverseNX forcing handheld — skip the APM docked check and fall
                // through to charger-type detection so handheld sub-profiles still work
                mode = 0;
            }
        }

        if (mode) {
            return HocClkProfile_Docked;
        }

        PsmChargerType chargerType;

        rc = psmGetChargerType(&chargerType);
        ASSERT_RESULT_OK(rc, "psmGetChargerType");

        if (chargerType == PsmChargerType_EnoughPower) {
            return HocClkProfile_HandheldChargingOfficial;
        } else if (chargerType == PsmChargerType_LowPower) {
            return HocClkProfile_HandheldChargingUSB;
        }

        return HocClkProfile_Handheld;
    }

}