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
        fileUtils::LogLine("[kip] GetKipData start (read-only, build: v6)");

        // Refresh() returns false if the file mtime is unchanged AND
        // config is already loaded. That's normal on subsequent calls.
        // For the very first call (during Initialize), config may not
        // be loaded yet - in that case we still proceed to read the KIP
        // because GetConfigValues will give us a zeroed struct that we
        // then populate from the KIP.
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

        HocClkConfigValueList configValues;
        config::GetConfigValues(&configValues);

        /* ----------------------------------------------------------
         * Populate config values from the KIP.
         * Order and field names match official kip.cpp:GetKipData.
         * --------------------------------------------------------*/
        configValues.values[KipConfigValue_custRev]                = cust_get_cust_rev(&table);
        configValues.values[KipConfigValue_hpMode]                 = cust_get_hp_mode(&table);

        configValues.values[KipConfigValue_commonEmcMemVolt]       = cust_get_common_emc_volt(&table);
        configValues.values[KipConfigValue_eristaEmcMaxClock]      = cust_get_erista_emc_max(&table);
        configValues.values[KipConfigValue_eristaEmcMaxClock1]     = cust_get_erista_emc_max1(&table);
        configValues.values[KipConfigValue_eristaEmcMaxClock2]     = cust_get_erista_emc_max2(&table);
        configValues.values[KipConfigValue_marikoEmcMaxClock]      = cust_get_mariko_emc_max(&table);
        configValues.values[KipConfigValue_marikoEmcVddqVolt]      = cust_get_mariko_emc_vddq(&table);
        configValues.values[KipConfigValue_emcDvbShift]            = cust_get_emc_dvb_shift(&table);
        configValues.values[KipConfigValue_marikoSocVmax]          = cust_get_marikoSocVmax(&table);

        configValues.values[KipConfigValue_t1_tRCD]                = cust_get_tRCD(&table);
        configValues.values[KipConfigValue_t2_tRP]                 = cust_get_tRP(&table);
        configValues.values[KipConfigValue_t3_tRAS]                = cust_get_tRAS(&table);
        configValues.values[KipConfigValue_t4_tRRD]                = cust_get_tRRD(&table);
        configValues.values[KipConfigValue_t5_tRFC]                = cust_get_tRFC(&table);
        configValues.values[KipConfigValue_t6_tRTW]                = cust_get_tRTW(&table);
        configValues.values[KipConfigValue_t7_tWTR]                = cust_get_tWTR(&table);
        configValues.values[KipConfigValue_t8_tREFI]               = cust_get_tREFI(&table);
        configValues.values[KipConfigValue_stepMode]               = cust_get_step_mode(&table);

        configValues.values[KipConfigValue_timingEmcTbreak]        = cust_get_timing_emc_tbreak(&table);
        configValues.values[KipConfigValue_low_t6_tRTW]            = cust_get_low_t6_tRTW(&table);
        configValues.values[KipConfigValue_low_t7_tWTR]            = cust_get_low_t7_tWTR(&table);
        configValues.values[KipConfigValue_t2_tRP_cap]             = cust_get_tRP_cap(&table);

        configValues.values[KipConfigValue_read_latency_1333]      = cust_get_read_latency_1333(&table);
        configValues.values[KipConfigValue_read_latency_1600]      = cust_get_read_latency_1600(&table);
        configValues.values[KipConfigValue_read_latency_1866]      = cust_get_read_latency_1866(&table);
        configValues.values[KipConfigValue_read_latency_2133]      = cust_get_read_latency_2133(&table);

        configValues.values[KipConfigValue_write_latency_1333]     = cust_get_write_latency_1333(&table);
        configValues.values[KipConfigValue_write_latency_1600]     = cust_get_write_latency_1600(&table);
        configValues.values[KipConfigValue_write_latency_1866]     = cust_get_write_latency_1866(&table);
        configValues.values[KipConfigValue_write_latency_2133]     = cust_get_write_latency_2133(&table);

        configValues.values[KipConfigValue_mem_burst_read_latency]  = cust_get_burst_read_lat(&table);
        configValues.values[KipConfigValue_mem_burst_write_latency] = cust_get_burst_write_lat(&table);

        configValues.values[KipConfigValue_eristaCpuUV]            = cust_get_erista_cpu_uv(&table);
        configValues.values[KipConfigValue_eristaCpuVmin]          = cust_get_eristaCpuVmin(&table);
        configValues.values[KipConfigValue_eristaCpuMaxVolt]       = cust_get_erista_cpu_max_volt(&table);
        configValues.values[KipConfigValue_eristaCpuUnlock]        = cust_get_eristaCpuUnlock(&table);

        configValues.values[KipConfigValue_marikoCpuUVLow]         = cust_get_mariko_cpu_uv_low(&table);
        configValues.values[KipConfigValue_marikoCpuUVHigh]        = cust_get_mariko_cpu_uv_high(&table);
        configValues.values[KipConfigValue_tableConf]              = cust_get_table_conf(&table);
        configValues.values[KipConfigValue_marikoCpuLowVmin]       = cust_get_mariko_cpu_low_vmin(&table);
        configValues.values[KipConfigValue_marikoCpuHighVmin]      = cust_get_mariko_cpu_high_vmin(&table);
        configValues.values[KipConfigValue_marikoCpuMaxVolt]       = cust_get_mariko_cpu_max_volt(&table);
        configValues.values[KipConfigValue_marikoCpuMaxClock]      = cust_get_marikoCpuMaxClock(&table);

        configValues.values[KipConfigValue_eristaCpuBoostClock]    = cust_get_erista_cpu_boost(&table);
        configValues.values[KipConfigValue_marikoCpuBoostClock]    = cust_get_mariko_cpu_boost(&table);

        configValues.values[KipConfigValue_eristaGpuUV]            = cust_get_erista_gpu_uv(&table);
        configValues.values[KipConfigValue_eristaGpuVmin]          = cust_get_erista_gpu_vmin(&table);
        configValues.values[KipConfigValue_marikoGpuUV]            = cust_get_mariko_gpu_uv(&table);
        configValues.values[KipConfigValue_marikoGpuVmin]          = cust_get_mariko_gpu_vmin(&table);
        configValues.values[KipConfigValue_marikoGpuVmax]          = cust_get_mariko_gpu_vmax(&table);
        configValues.values[KipConfigValue_commonGpuVoltOffset]    = cust_get_common_gpu_offset(&table);

        // gpuSpeedo: official prefers the live fuse value over the KIP field.
        // We do the same - fuse data is more reliable than a stored copy.
        configValues.values[KipConfigValue_gpuSpeedo]              = board::GetFuseData()->gpuSpeedo;

        for (int i = 0; i < 24; i++) {
            configValues.values[KipConfigValue_g_volt_76800 + i]   = cust_get_mariko_gpu_volt(&table, i);
        }

        for (int i = 0; i < 27; i++) {
            configValues.values[KipConfigValue_g_volt_e_76800 + i] = cust_get_erista_gpu_volt(&table, i);
        }

        configValues.values[KipConfigValue_t7_tWTR_fine_tune]      = cust_get_tWTR_fine_tune(&table);
        configValues.values[KipConfigValue_t6_tRTW_fine_tune]      = cust_get_tRTW_fine_tune(&table);

        /* ----------------------------------------------------------
         * Commit. SetConfigValues(false) means "don't persist to
         * config.ini" - these are runtime values sourced from the KIP
         * and shouldn't end up written to user config.
         * --------------------------------------------------------*/
        if (sizeof(HocClkConfigValueList) > sizeof(configValues)) {
            fileUtils::LogLine("[kip] config buffer size mismatch - aborting commit");
            return;
        }

        if (!config::SetConfigValues(&configValues, false)) {
            fileUtils::LogLine("[kip] config::SetConfigValues failed");
            return;
        }

        fileUtils::LogLine("[kip] KIP loaded (read-only). CustRev=%lu marikoGpuUV=%lu marikoCpuUVLow=%lu marikoCpuUVHigh=%lu tableConf=%lu marikoEmcMaxClock=%lu",
            configValues.values[KipConfigValue_custRev],
            configValues.values[KipConfigValue_marikoGpuUV],
            configValues.values[KipConfigValue_marikoCpuUVLow],
            configValues.values[KipConfigValue_marikoCpuUVHigh],
            configValues.values[KipConfigValue_tableConf],
            configValues.values[KipConfigValue_marikoEmcMaxClock]);

        // Dump every KIP-derived value to the log so the user can verify
        // the read-only path matched what their 3rd-party tool wrote.
        for (u64 i = KipConfigValue_hpMode; i < HocClkConfigValue_EnumMax; i++) {
            fileUtils::LogLine("[kip] %s: %ld",
                hocclkFormatConfigValue((HocClkConfigValue)i, false),
                configValues.values[i]);
        }
    }

} // namespace kip
