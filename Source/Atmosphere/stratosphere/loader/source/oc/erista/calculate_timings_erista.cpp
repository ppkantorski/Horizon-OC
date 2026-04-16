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

namespace ams::ldr::hoc::pcv::erista {

    void CalculateTimings(double tCK_avg) {
        tR2W    = FLOOR(FLOOR((5.0 / tCK_avg) + ((FLOOR(48.0 / WL) - 0.478) * 3.0)) / 1.501) + RL - (C.t6_tRTW * 3) + finetRTW;
        tWTPDEN = CEIL(((1.803 / tCK_avg) + MAX(RL + (2.694 / tCK_avg), static_cast<double>(tW2P))) + (BL / 2));
        tW2R    = FLOOR(MAX((5.020 / tCK_avg) + 1.130, WL - MAX(-CEIL(0.258 * (WL - RL)), 1.964)) * 1.964) + WL - CEIL(tWTR / tCK_avg) + finetWTR;

        pdex2rw  = CEIL((CEIL(12.335 - tCK_avg) + (7.430 / tCK_avg) - CEIL(tCK_avg * 11.361)));
        tCLKSTOP = FLOOR(MIN(8.488 / tCK_avg, 23.0)) + 8.0;

        const double tMMRI = tRCD + (tCK_avg * 3);
        pdex2mrr           = tMMRI + 10;
    }

}
