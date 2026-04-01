/*
 * Copyright (c) ppkantorski (bord2death)
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

/*
 * SDx actual min is 625 mV. Multipliers 0/1 reserved.
 * SD0 max is 1400 mV
 * SD1 max is 1550 mV
 * SD2 max is 3787.5 mV
 * SD3 max is 3787.5 mV
 */

/*
* Switch Power domains (max77620):
* Name  | Usage         | uV step | uV min | uV default | uV max  | Init
*-------+---------------+---------+--------+------------+---------+------------------
*  sd0  | SoC           | 12500   | 600000 |  625000    | 1400000 | 1.125V (pkg1.1)
*  sd1  | SDRAM         | 12500   | 600000 | 1125000    | 1125000 | 1.1V   (pkg1.1)
*  sd2  | ldo{0-1, 7-8} | 12500   | 600000 | 1325000    | 1350000 | 1.325V (pcv)
*  sd3  | 1.8V general  | 12500   | 600000 | 1800000    | 1800000 |
*  ldo0 | Display Panel | 25000   | 800000 | 1200000    | 1200000 | 1.2V   (pkg1.1)
*  ldo1 | XUSB, PCIE    | 25000   | 800000 | 1050000    | 1050000 | 1.05V  (pcv)
*  ldo2 | SDMMC1        | 50000   | 800000 | 1800000    | 3300000 |
*  ldo3 | GC ASIC       | 50000   | 800000 | 3100000    | 3100000 | 3.1V   (pcv)
*  ldo4 | RTC           | 12500   | 800000 |  850000    |  850000 | 0.85V  (AO, pcv)
*  ldo5 | GC Card       | 50000   | 800000 | 1800000    | 1800000 | 1.8V   (pcv)
*  ldo6 | Touch, ALS    | 50000   | 800000 | 2900000    | 2900000 | 2.9V   (pcv)
*  ldo7 | XUSB          | 50000   | 800000 | 1050000    | 1050000 | 1.05V  (pcv)
*  ldo8 | XUSB, DP, MCU | 50000   | 800000 | 1050000    | 2800000 | 1.05V/2.8V (pcv)
*/


// GPIOs T210: 3: 3.3V, 5: CPU PMIC, 6: GPU PMIC, 7: DSI/VI 1.2V powered by ldo0.

/*
 * OTP:  T210 - T210B01:
 * SD0:  1.0V   1.05V - SoC. EN Based on FPSSRC.
 * SD1:  1.15V  1.1V  - DRAM for T210. EN Based on FPSSRC.
 * SD2:  1.35V  1.35V
 * SD3:  1.8V   1.8V
 * All powered off?
 * LDO0:   --   --    - Display
 * LDO1: 1.05V  1.05V
 * LDO2:   --   --    - SD
 * LDO3: 3.1V   3.1V  - GC ASIC
 * LDO4: 1.0V   0.8V  - Needed for RTC domain on T210.
 * LDO5: 3.1V   3.1V
 * LDO6: 2.8V   2.9V  - Touch.
 * LDO7: 1.05V  1.0V
 * LDO8: 1.05V  1.0V
 */

/*
* MAX77620_AME_GPIO: control GPIO modes (bits 0 - 7 correspond to GPIO0 - GPIO7); 0 -> GPIO, 1 -> alt-mode
* MAX77620_REG_GPIOx: 0x9 sets output and enable
*/

typedef enum {
    PcvPowerDomain_Max77620_Sd0  = 0,
    PcvPowerDomain_Max77620_Sd1  = 1,
    PcvPowerDomain_Max77620_Sd2  = 2,
    PcvPowerDomain_Max77620_Sd3  = 3,
    PcvPowerDomain_Max77620_Ldo0 = 4,
    PcvPowerDomain_Max77620_Ldo1 = 5,
    PcvPowerDomain_Max77620_Ldo2 = 6,
    PcvPowerDomain_Max77620_Ldo3 = 7,
    PcvPowerDomain_Max77620_Ldo4 = 8,
    PcvPowerDomain_Max77620_Ldo5 = 9,
    PcvPowerDomain_Max77620_Ldo6 = 10,
    PcvPowerDomain_Max77620_Ldo7 = 11,
    PcvPowerDomain_Max77620_Ldo8 = 12,
    PcvPowerDomain_Max77621_Cpu  = 13,
    PcvPowerDomain_Max77621_Gpu  = 14,
    PcvPowerDomain_Max77812_Cpu  = 15,
    PcvPowerDomain_Max77812_Gpu  = 16,
    PcvPowerDomain_Max77812_Dram = 17,
} PowerDomain;

typedef enum {
    PcvPowerDomainId_Max77620_Sd0  = 0x3A000080,
    PcvPowerDomainId_Max77620_Sd1  = 0x3A000081, // vdd2
    PcvPowerDomainId_Max77620_Sd2  = 0x3A000082,
    PcvPowerDomainId_Max77620_Sd3  = 0x3A000083,
    PcvPowerDomainId_Max77620_Ldo0 = 0x3A0000A0,
    PcvPowerDomainId_Max77620_Ldo1 = 0x3A0000A1,
    PcvPowerDomainId_Max77620_Ldo2 = 0x3A0000A2,
    PcvPowerDomainId_Max77620_Ldo3 = 0x3A0000A3,
    PcvPowerDomainId_Max77620_Ldo4 = 0x3A0000A4,
    PcvPowerDomainId_Max77620_Ldo5 = 0x3A0000A5,
    PcvPowerDomainId_Max77620_Ldo6 = 0x3A0000A6,
    PcvPowerDomainId_Max77620_Ldo7 = 0x3A0000A7,
    PcvPowerDomainId_Max77620_Ldo8 = 0x3A0000A8,
    PcvPowerDomainId_Max77621_Cpu  = 0x3A000003,
    PcvPowerDomainId_Max77621_Gpu  = 0x3A000004,
    PcvPowerDomainId_Max77812_Cpu  = 0x3A000003,
    PcvPowerDomainId_Max77812_Gpu  = 0x3A000004,
    PcvPowerDomainId_Max77812_Dram = 0x3A000005, // vddq
} PowerDomainId;