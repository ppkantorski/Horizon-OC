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


#pragma once

#include <cstdio>
#include <string>
#include <cstdint>
#include <hocclk/board.h>

#define FREQ_DEFAULT_TEXT "Do not override"

static inline std::string formatListFreqMHz(std::uint32_t mhz)
{
    if(mhz == 0)
    {
        return FREQ_DEFAULT_TEXT;
    }

    char buf[10];
    return std::string(buf, snprintf(buf, sizeof(buf), "%u MHz", mhz));
}

static inline std::string formatListFreqHz(uint32_t hz) { return formatListFreqMHz(hz / 1000000); }

static inline std::string formatListFreqMem(uint32_t mhz, RamDisplayUnit unit)
{
    if(mhz == 0)
        return FREQ_DEFAULT_TEXT;

    uint32_t mts = mhz * 2;
    char buf[24];
    switch(unit)
    {
        case RamDisplayUnit_MHz:
            snprintf(buf, sizeof(buf), "%u MHz", mhz);
            break;
        case RamDisplayUnit_MHzMTs:
            snprintf(buf, sizeof(buf), "%u MHz (%u MT/s)", mhz, mts);
            break;
        case RamDisplayUnit_MTs:
        default:
            snprintf(buf, sizeof(buf), "%u MT/s", mts);
            break;
    }
    return buf;
}

static inline std::string formatListFreqHzMem(uint32_t hz, RamDisplayUnit unit)
{
    return formatListFreqMem(hz / 1000000, unit);
}

static inline std::string formatMemClockKhzLabel(uint32_t khz, RamDisplayUnit unit)
{
    uint32_t mhz = khz / 1000;
    uint32_t mts = khz / 500;
    char buf[32];
    switch(unit)
    {
        case RamDisplayUnit_MHz:
            snprintf(buf, sizeof(buf), "%u MHz", mhz);
            break;
        case RamDisplayUnit_MHzMTs:
            snprintf(buf, sizeof(buf), "%u MHz (%u MT/s)", mhz, mts);
            break;
        case RamDisplayUnit_MTs:
        default:
            snprintf(buf, sizeof(buf), "%u MT/s", mts);
            break;
    }
    return buf;
}
