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

#include <switch.h>
#include <time.h>
#include <vector>
#include <string>
#include <atomic>
#include <cstdarg>
#include <hocclk.h>

#define FILE_CONFIG_DIR "/config/" CONFIG_DIR
#define FILE_FLAG_CHECK_INTERVAL_NS (10000ULL * 1000000000ULL)
#define FILE_CONTEXT_CSV_PATH FILE_CONFIG_DIR "/context.csv"
#define FILE_LOG_FLAG_PATH FILE_CONFIG_DIR "/log.flag"
#define FILE_LOG_FILE_PATH FILE_CONFIG_DIR "/log.txt"

namespace fileUtils {

    void Exit();
    Result Initialize();
    bool IsInitialized();
    void InitializeAsync();
    void WriteContextToCsv(const HocClkContext* context);

#ifdef ENABLE_LOGGING
    bool IsLogEnabled();
    void LogLine(const char* format, ...);
#else
    // Logging compiled out (build with `make ENABLE_LOGGING=1` to enable).
    //
    // LogLine becomes a variadic-template no-op: every call site still
    // type-checks, but the empty inline body means the calls — and their
    // format-string literals, which become unreferenced — are removed by
    // -Os/LTO.  This strips both the logging code and its strings from
    // .text/.rodata without touching any of the ~100 call sites.
    static inline bool IsLogEnabled() { return false; }
    template <typename... Args>
    static inline void LogLine(Args&&...) {}
#endif

}