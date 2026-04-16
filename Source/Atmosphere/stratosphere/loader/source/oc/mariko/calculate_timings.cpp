/*
 * Copyright (c) Lightos_
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

#include "../oc_common.hpp"
#include "../mtc_timing_value.hpp"
#include "timing_tables.hpp"

namespace ams::ldr::hoc::pcv::mariko {

    void GetRext() {
        if (auto r = FindRext()) {
            rext = r->rext;
            return;
        }

        /* Fallback. */
        rext = 0x1A;
    }

    void CalculateMrw2() {
        static const u8 rlMapDBI[8] = {
            6, 12, 16, 22, 28, 32, 36, 40
        };

        static const u8 wlMapSetA[8] = {
            4, 6, 8, 10, 12, 14, 16, 18
        };

        u32 rlIndex = 0;
        u32 wlIndex = 0;

        for (u32 i = 0; i < std::size(rlMapDBI); ++i) {
            if (rlMapDBI[i] == RL) {
                rlIndex = i;
                break;
            }
        }

        for (u32 i = 0; i < std::size(wlMapSetA); ++i) {
            if (wlMapSetA[i] == WL) {
                wlIndex = i;
                break;
            }
        }

        mrw2 = static_cast<u8>(((rlIndex & 0x7) | ((wlIndex & 0x7) << 3) | ((0 & 0x1) << 6)));
    }

    void CalculateTimings() {
        GetRext();
        CalculateMrw2();
    }

}
