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
#include <hocclk.h>
#include "../board/board.hpp"
#include "clock_manager.hpp"
#include <cstring>
#include "../file/file_utils.hpp"
#include "../board/board.hpp"
#include "../file/errors.hpp"
#include "../file/config.hpp"
#include "../hos/integrations.hpp"
#include "../util/lockable_mutex.h"

namespace governor {
    extern bool isCpuGovernorInBoostMode;
    extern bool isVRREnabled;
    extern bool isGpuGovernorEnabled;
    extern bool isCpuGovernorEnabled;
    extern bool lastGpuGovernorState;
    extern bool lastCpuGovernorState;
    extern bool lastVrrGovernorState;
    void startThreads();
    void exitThreads();
    void HandleGovernor(uint32_t targetHz);
}