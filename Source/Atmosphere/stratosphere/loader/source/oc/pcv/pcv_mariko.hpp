/*
 * Copyright (C) Switch-OC-Suite
 *
 * Copyright (c) 2023 hanai3Bi
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
 */

#pragma once

#include "../oc_common.hpp"
#include "pcv_common.hpp"
#include "pcv_asm.hpp"

namespace ams::ldr::hoc::pcv::mariko {

    constexpr cvb_entry_t CpuCvbTableDefault[] = {
        {  204000, {  721589, -12695, 27 }, {         } },
        {  306000, {  747134, -14195, 27 }, {         } },
        {  408000, {  776324, -15705, 27 }, {         } },
        {  510000, {  809160, -17205, 27 }, {         } },
        {  612000, {  845641, -18715, 27 }, {         } },
        {  714000, {  885768, -20215, 27 }, {         } },
        {  816000, {  929540, -21725, 27 }, {         } },
        {  918000, {  976958, -23225, 27 }, {         } },
        { 1020000, { 1028021, -24725, 27 }, { 1120000 } },
        { 1122000, { 1082730, -26235, 27 }, { 1120000 } },
        { 1224000, { 1141084, -27735, 27 }, { 1120000 } },
        { 1326000, { 1203084, -29245, 27 }, { 1120000 } },
        { 1428000, { 1268729, -30745, 27 }, { 1120000 } },
        { 1581000, { 1374032, -33005, 27 }, { 1120000 } },
        { 1683000, { 1448791, -34505, 27 }, { 1120000 } },
        { 1785000, { 1527196, -36015, 27 }, { 1120000 } },
        { 1887000, { 1609246, -37515, 27 }, { 1120000 } },
        { 1963500, { 1675751, -38635, 27 }, { 1120000 } },
        {                                               },
    };

    constexpr u32 CpuClkOfficial  = 1963'500;
    constexpr u32 CpuVoltOfficial = 1120;
    constexpr u32 CpuVminOfficial = 620;

    static const u32 cpuVoltagePatchValues[]  = { 850, 38, 1120, 1000, 100, 1000, 0 };
    static const s32 cpuVoltagePatchOffsets[] = {  -2, -1,    5,    6,   7,    8, 9 };
    static_assert(sizeof(cpuVoltagePatchValues) == sizeof(cpuVoltagePatchOffsets), "Invalid cpuVoltagePatch size");

    static const u32 cpuVoltThermalData[] = { 620, 1120, 20000, 620, 1120, 70000, 950, 1132, 0, 950, 1227, 0 };

    static const u32 allowedCpuMaxFrequencies[] = { 1'963'500, 2'091'000, 2'193'000, 2'295'000, 2'397'000, 2'499'000, 2'601'000, 2'703'000, };

    constexpr cvb_entry_t GpuCvbTableDefault[] = {
        // GPUB01_NA_CVB_TABLE
        {   76800, {}, {  610000,                                 } },
        {  153600, {}, {  610000,                                 } },
        {  230400, {}, {  610000,                                 } },
        {  307200, {}, {  610000,                                 } },
        {  384000, {}, {  610000,                                 } },
        {  460800, {}, {  610000,                                 } },
        {  537600, {}, {  801688, -10900, -163, 298, -10599, 162, } },
        {  614400, {}, {  824214,  -5743, -452, 238,  -6325,  81, } },
        {  691200, {}, {  848830,  -3903, -552, 119,  -4030,  -2, } },
        {  768000, {}, {  891575,  -4409, -584,   0,  -2849,  39, } },
        {  844800, {}, {  940071,  -5367, -602, -60,    -63, -93, } },
        {  921600, {}, {  986765,  -6637, -614, -179,  1905, -13, } },
        {  998400, {}, { 1098475, -13529, -497, -179,  3626,   9, } },
        { 1075200, {}, { 1163644, -12688, -648,    0,  1077,  40, } },
        { 1152000, {}, { 1204812,  -9908, -830,    0,  1469, 110, } },
        { 1228800, {}, { 1277303, -11675, -859,    0,  3722, 313, } },
        { 1267200, {}, { 1335531, -12567, -867,    0,  3681, 559, } },
        {                                                           },
    };

    constexpr u32 GpuClkPllMax    = 1300'000'000;
    constexpr u32 GpuClkPllLimit  = 2'600'000;
    constexpr u32 GpuVminOfficial = 610;

    static const u32 gpuDVFSPattern[] = { 1050, 1000, 100, 1000, 10, };
    static const u32 gpuVoltThermalPattern[] = { 800, 1120, 0, 610, 1120, 20000, 610, 1120, 30000, 610, 1120, 50000, 610, 1120, 70000, 610, 1120, 90000, };
    static_assert(sizeof(gpuVoltThermalPattern) == 72, "Invalid gpuVoltThermalPattern");

    /* GPU Max Clock asm Pattern:
        *
        * MOV W11, #0x1000 MOV (wide immediate)                0x1000                              0xB (11)
        *  sf | opc |                 | hw  |                   imm16                        |      Rd
        * #31 |30 29|28 27 26 25 24 23|22 21|20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5 |4  3  2  1  0
        *   0 | 1 0 | 1  0  0  1  0  1| 0  0| 0  0  0  1  0  0  0  0  0  0  0  0  0  0  0  0 |0  1  0  1  1
        *
        * MOVK W11, #0xE, LSL#16     <shift>16                    0xE                              0xB (11)
        *  sf | opc |                 | hw  |                   imm16                        |      Rd
        * #31 |30 29|28 27 26 25 24 23|22 21|20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5 |4  3  2  1  0
        *   0 | 1 1 | 1  0  0  1  0  1| 0  1| 0  0  0  0  0  0  0  0  0  0  0  0  1  1  1  0 |0  1  0  1  1
        */
    inline constexpr u32 GpuAsmPattern[] = { 0x52820000, 0x72A001C0 };

    inline bool GpuMaxClockPatternFn(u32 *ptr32) {
        return asm_compare_no_rd(*ptr32, GpuAsmPattern[0]);
    }

    constexpr emc_dvb_dvfs_table_t EmcDvbTableDefault[] = {
        {  204000, { 637, 637, 637, } },
        {  408000, { 637, 637, 637, } },
        {  800000, { 637, 637, 637, } },
        { 1065600, { 637, 637, 637, } },
        { 1331200, { 650, 637, 637, } },
        { 1600000, { 675, 650, 637, } },
    };

    constexpr u32 EmcClkOSAlt = 1331'200;
    constexpr u32 EmcClkPllmLimit = 2133'000'000;
    constexpr u32 EmcVddqDefault = 600'000;
    constexpr u32 MemVdd2Default = 1100'000;

    constexpr u32 MTC_TABLE_REV = 3;

    void Patch(uintptr_t mapped_nso, size_t nso_size);

}
