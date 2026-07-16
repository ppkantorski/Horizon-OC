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
 */

/*
 * sys-clk-hoc kip.cpp - READ-ONLY variant
 *
 * Behaviour vs. official Horizon-OC:
 *   - Reads sdmc:/atmosphere/kips/hoc.kip on startup and populates the
 *     KipConfigValue_* portion of the in-memory config exactly the same
 *     way the official sysmodule does.
 *   - Does NOT write the KIP. Ever. The CRC32-mismatch -> SetKipData
 *     branch and the IsFirstLoad-write branch are both removed.
 *   - Does NOT call notification::writeNotification. All user-facing
 *     status messages from this module go to the log instead - overlay
 *     and any 3rd-party KIP tool own the notification surface.
 *   - SetKipData() is retained as a logging no-op so the symbol declared
 *     in kip.hpp resolves at link time even if some future caller pulls
 *     it in. Calling it is a hard error from this module's perspective:
 *     the KIP is treated as authoritative and externally managed.
 *
 * Why this exists:
 *   GetMaxAllowedHz() in clock_manager.cpp branches on
 *   KipConfigValue_marikoGpuUV. If the KIP isn't read, that value is 0,
 *   which silently caps the GPU at 614.4 MHz regardless of what the
 *   user's KIP says. Several CPU-side voltage helpers
 *   (KipConfigValue_marikoCpuUVLow/UVHigh/tableConf, eristaCpuUV) feed
 *   board::SetDfllTunings when LiveCpuUv is enabled - those would also
 *   sit at zero. Both can produce subtle SoC-domain instability when
 *   the EMC is pushed to high frequencies.
 */

#include "kip.hpp"
#include "board/board.hpp"
#include "file_utils.hpp"

namespace kip {

    bool kipAvailable = false;

    static constexpr const char* KIP_PATH = "sdmc:/atmosphere/kips/hoc.kip";

    /*
     * SetKipData(): no-op. This sysmodule never writes the KIP.
     * Defined here only so the symbol resolves; nothing in sys-clk-hoc
     * calls it. If you find this in a log, something is wrong upstream.
     */
    void SetKipData()
    {
        fileUtils::LogLine("[kip] SetKipData() called but ignored - "
                           "this build never writes the KIP. KIP is managed externally.");
    }

    /*
     * GetKipData(): read-only KIP loader.
     * Mirrors the population block of official Horizon-OC's GetKipData
     * minus the CRC32-resync write, the IsFirstLoad write, and the
     * notification calls.
     */
    void GetKipData()
    {
        fileUtils::LogLine("[kip] GetKipData start (read-only, memory-only, 2.5.0 layout)");

        // Ensure config is loaded before we push KIP-derived values into it.
        config::Refresh();

        FILE* fp = fopen(KIP_PATH, "r");
        if (fp == NULL) {
            fileUtils::LogLine("[kip] %s not found - KIP-derived config values stay at defaults", KIP_PATH);
            kipAvailable = false;
            return;
        }
        fclose(fp);
        kipAvailable = true;

        CustomizeTable table;
        if (!cust_read_and_cache(KIP_PATH, &table)) {
            fileUtils::LogLine("[kip] failed to parse CUST table from %s", KIP_PATH);
            kipAvailable = false;
            return;
        }

        /* ------------------------------------------------------------------
         * Push the handful of KIP values sys-clk-hoc actually consumes at
         * runtime straight into the in-memory config. These are MEMORY-ONLY:
         * config::SetConfigValue() skips disk persistence for indices >=
         * KipConfigValue_FIRST, so nothing is mirrored to config.ini and a
         * config reload preserves them (see config.cpp Close()). The KIP
         * itself remains the single source of truth for every other value
         * (read directly by the overlay / HOC Toolkit).
         *   - marikoGpuUV  -> GetMaxAllowedHz() GPU cap in clock_manager
         *   - eristaCpuUV / marikoCpuUVLow / marikoCpuUVHigh / tableConf
         *                  -> board::SetDfllTunings when LiveCpuUv is enabled
         * ----------------------------------------------------------------*/
        config::SetConfigValue(KipConfigValue_eristaCpuUV,     cust_get_erista_cpu_uv(&table),      true);
        config::SetConfigValue(KipConfigValue_marikoCpuUVLow,  cust_get_mariko_cpu_uv_low(&table),  true);
        config::SetConfigValue(KipConfigValue_marikoCpuUVHigh, cust_get_mariko_cpu_uv_high(&table), true);
        config::SetConfigValue(KipConfigValue_tableConf,       cust_get_table_conf(&table),         true);
        config::SetConfigValue(KipConfigValue_marikoGpuUV,     cust_get_mariko_gpu_uv(&table),      true);

        fileUtils::LogLine("[kip] KIP loaded (read-only, memory-only). CustRev=%lu marikoGpuUV=%lu eristaCpuUV=%lu marikoCpuUVLow=%lu marikoCpuUVHigh=%lu tableConf=%lu",
            (unsigned long)cust_get_cust_rev(&table),
            (unsigned long)config::GetConfigValue(KipConfigValue_marikoGpuUV),
            (unsigned long)config::GetConfigValue(KipConfigValue_eristaCpuUV),
            (unsigned long)config::GetConfigValue(KipConfigValue_marikoCpuUVLow),
            (unsigned long)config::GetConfigValue(KipConfigValue_marikoCpuUVHigh),
            (unsigned long)config::GetConfigValue(KipConfigValue_tableConf));
    }

} // namespace kip