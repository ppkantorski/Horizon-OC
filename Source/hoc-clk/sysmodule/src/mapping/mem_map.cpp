/*
 * Copyright (c) Souldbminer and Horizon OC Contributors
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
#include "../file/file_utils.hpp"

Result QueryMemoryMapping(u64* virtaddr, u64 physaddr, u64 size) {
    if(hosversionAtLeast(10,0,0)) {
        u64 out_size;
        return svcQueryMemoryMapping(virtaddr, &out_size, physaddr, size);
    } else {
        return svcLegacyQueryIoMapping(virtaddr, physaddr, size);
    }
}

Result MapAddress(u64 &va, const u64 &physAddr, const char *name) {
    Result mapResult = QueryMemoryMapping(&va, physAddr, 0x1000);
    if (R_FAILED(mapResult)) {
        fileUtils::LogLine("Failed to map %s! %u", name, R_DESCRIPTION(mapResult));
    }

    return mapResult;
}
