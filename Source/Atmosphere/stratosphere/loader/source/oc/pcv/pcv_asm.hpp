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

#include <stratosphere.hpp>

namespace ams::ldr::hoc::pcv {

    constexpr u32 NopIns = 0x1f2003d5;

    inline auto asm_compare_no_rd = [](u32 ins1, u32 ins2) {
        return ((ins1 ^ ins2) >> 5) == 0;
    };

    inline auto asm_get_rd = [](u32 ins) {
        return ins & ((1 << 5) - 1);
    };

    inline auto asm_set_rd = [](u32 ins, u8 rd) {
        return (ins & 0xFFFFFFE0) | (rd & 0x1F);
    };

    inline auto asm_set_imm16 = [](u32 ins, u16 imm) {
        return (ins & 0xFFE0001F) | ((imm & 0xFFFF) << 5);
    };

    inline auto AsmGetImm16 = [](u32 ins) {
        return static_cast<u16>((ins >> 5) & 0xFFFF);
    };

    inline auto AsmCompareBrNoRd = [](u32 ins1, u32 ins2) {
        constexpr u32 RegMask = ~(((1 << 5) - 1) << 5);
        return ((ins1 & RegMask) ^ (ins2 & RegMask)) == 0;
    };

    inline auto AsmCompareAddNoImm12 = [](u32 ins1, u32 ins2) {
        constexpr u32 Imm12Mask = ~(((1 << 12) - 1) << 10);
        return ((ins1 & Imm12Mask) ^ (ins2 & Imm12Mask)) == 0;
    };

    inline auto AsmCompareAdrpNoImm = [](u32 ins1, u32 ins2) {
        constexpr u32 ImmMask = ~((((1 << 2) - 1) << 29) | (((1 << 19) - 1) << 5));
        return ((ins1 & ImmMask) ^ (ins2 & ImmMask)) == 0;
    };

}
