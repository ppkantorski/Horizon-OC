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

#include <sysclk.h>
#include <switch.h>
#include <nxExt.h>
#include <cmath>
#include <battery.h>
#include <pwm.h>
#include "board.hpp"
#include "../soctherm.hpp"

namespace board {

    s32 GetTemperatureMilli(SysClkThermalSensor sensor) {
        s32 millis = 0;
        BatteryChargeInfo info;

        soctherm::TSensorTemps temps = {};
        soctherm::ReadSensors(temps);

        switch(sensor) {
            case SysClkThermalSensor_SOC: {
                millis = tmp451TempSoc();
                break;
            }
            case SysClkThermalSensor_PCB: {
                millis = tmp451TempPcb();
                break;
            }
            case SysClkThermalSensor_Skin: {
                if (HOSSVC_HAS_TC) {
                    Result rc;
                    rc = tcGetSkinTemperatureMilliC(&millis);
                    ASSERT_RESULT_OK(rc, "tcGetSkinTemperatureMilliC");
                }
                break;
            }
            case HorizonOCThermalSensor_Battery: {
                batteryInfoGetChargeInfo(&info);
                millis = batteryInfoGetTemperatureMiliCelsius(&info);
                break;
            }
            case HorizonOCThermalSensor_PMIC: {
                millis = 50000;
                break;
            }
            case HorizonOCThermalSensor_CPU: {
                millis = temps.cpu;
                break;
            }
            case HorizonOCThermalSensor_GPU: {
                millis = temps.gpu;
                break;
            }
            case HorizonOCThermalSensor_MEM: {
                millis = temps.mem;
                break;
            }
            case HorizonOCThermalSensor_PLLX: {
                millis = temps.pllx;
            }
            default: {
                ASSERT_ENUM_VALID(SysClkThermalSensor, sensor);
            }
        }

        return std::max(0, millis);
    }

    s32 GetPowerMw(SysClkPowerSensor sensor) {
        switch (sensor) {
            case SysClkPowerSensor_Now:
                return max17050PowerNow();
            case SysClkPowerSensor_Avg:
                return max17050PowerAvg();
            default:
                ASSERT_ENUM_VALID(SysClkPowerSensor, sensor);
        }

        return 0;
    }

}
