/*
 * kip.cpp - STUB
 *
 * KIP reading and writing has been removed from this sysmodule.
 * The KIP is managed externally by the user and must not be touched at runtime.
 * GetKipData() is a no-op; SetKipData() is never called by the IPC service.
 */

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

#include "kip.hpp"
#include "file_utils.hpp"

namespace kip {

    bool kipAvailable = false;

    // No-op: sysmodule never reads or writes the KIP.
    void GetKipData()
    {
        fileUtils::LogLine("[kip] KIP management disabled; using externally supplied KIP.");
        kipAvailable = false;
    }

    // SetKipData() is intentionally not implemented.
    // It is not exposed via IPC in this build.

} // namespace kip
