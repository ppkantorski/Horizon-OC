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
#include "board/board.hpp"
#include "file_utils.hpp"

#define CUST_REV 2
namespace kip {

    bool kipAvailable = false;

    void SetKipData()
    {
        // TODO: figure out if this REALLY causes issues (i doubt it)
        // if(board::GetSocType() == HocClkSocType_Mariko) {
        //     if(R_FAILED(I2c_BuckConverter_SetMvOut(&I2c_Mariko_DRAM_VDDQ, config::GetConfigValue(KipConfigValue_marikoEmcVddqVolt) / 1000))) {
        //         fileUtils::LogLine("[clock_manager] Failed set i2c vddq");
        //         notification::writeNotification("Horizon OC\nFailed to write I2C\nwhile setting vddq");
        //     }
        // }
        CustomizeTable table;
        FILE* fp;
        fp = fopen("sdmc:/atmosphere/kips/hoc.kip", "r");

        if (fp == NULL) {
            notification::writeNotification("Horizon OC\nKip opening failed");
            kipAvailable = false;
            return;
        } else {
            kipAvailable = true;
            fclose(fp);
        }

        if (!cust_read_and_cache("sdmc:/atmosphere/kips/hoc.kip", &table)) {
            fileUtils::LogLine("[kip] Failed to read KIP file");
            notification::writeNotification("Horizon OC\nKip read failed");
            return;
        }

        // if(cust_get_cust_rev(&table) != CUST_REV) {
        //     fileUtils::LogLine("Revision: %u", cust_get_cust_rev(&table));
        //     notification::writeNotification("Horizon OC\nKip version mismatch\nPlease reinstall Horizon OC");
        //     return;
        // }

        CUST_WRITE_FIELD_BATCH(&table, custRev, config::GetConfigValue(KipConfigValue_custRev));
        // CUST_WRITE_FIELD_BATCH(&table, mtcConf, config::GetConfigValue(KipConfigValue_mtcConf));
        CUST_WRITE_FIELD_BATCH(&table, hpMode, config::GetConfigValue(KipConfigValue_hpMode));

        CUST_WRITE_FIELD_BATCH(&table, commonEmcMemVolt, config::GetConfigValue(KipConfigValue_commonEmcMemVolt));
        CUST_WRITE_FIELD_BATCH(&table, eristaEmcMaxClock, config::GetConfigValue(KipConfigValue_eristaEmcMaxClock));
        CUST_WRITE_FIELD_BATCH(&table, eristaEmcMaxClock1, config::GetConfigValue(KipConfigValue_eristaEmcMaxClock1));
        CUST_WRITE_FIELD_BATCH(&table, eristaEmcMaxClock2, config::GetConfigValue(KipConfigValue_eristaEmcMaxClock2));
        CUST_WRITE_FIELD_BATCH(&table, marikoEmcMaxClock, config::GetConfigValue(KipConfigValue_marikoEmcMaxClock));
        CUST_WRITE_FIELD_BATCH(&table, marikoEmcVddqVolt, config::GetConfigValue(KipConfigValue_marikoEmcVddqVolt));
        CUST_WRITE_FIELD_BATCH(&table, emcDvbShift, config::GetConfigValue(KipConfigValue_emcDvbShift));

        CUST_WRITE_FIELD_BATCH(&table, t1_tRCD, config::GetConfigValue(KipConfigValue_t1_tRCD));
        CUST_WRITE_FIELD_BATCH(&table, t2_tRP, config::GetConfigValue(KipConfigValue_t2_tRP));
        CUST_WRITE_FIELD_BATCH(&table, t3_tRAS, config::GetConfigValue(KipConfigValue_t3_tRAS));
        CUST_WRITE_FIELD_BATCH(&table, t4_tRRD, config::GetConfigValue(KipConfigValue_t4_tRRD));
        CUST_WRITE_FIELD_BATCH(&table, t5_tRFC, config::GetConfigValue(KipConfigValue_t5_tRFC));
        CUST_WRITE_FIELD_BATCH(&table, t6_tRTW, config::GetConfigValue(KipConfigValue_t6_tRTW));
        CUST_WRITE_FIELD_BATCH(&table, t7_tWTR, config::GetConfigValue(KipConfigValue_t7_tWTR));
        CUST_WRITE_FIELD_BATCH(&table, t8_tREFI, config::GetConfigValue(KipConfigValue_t8_tREFI));
        CUST_WRITE_FIELD_BATCH(&table, stepMode, config::GetConfigValue(KipConfigValue_stepMode));

        CUST_WRITE_FIELD_BATCH(&table, timingEmcTbreak, config::GetConfigValue(KipConfigValue_timingEmcTbreak));
        CUST_WRITE_FIELD_BATCH(&table, low_t6_tRTW, config::GetConfigValue(KipConfigValue_low_t6_tRTW));
        CUST_WRITE_FIELD_BATCH(&table, low_t7_tWTR, config::GetConfigValue(KipConfigValue_low_t7_tWTR));
        CUST_WRITE_FIELD_BATCH(&table, t2_tRP_cap, config::GetConfigValue(KipConfigValue_t2_tRP_cap));

        CUST_WRITE_FIELD_BATCH(&table, readLatency1333, config::GetConfigValue(KipConfigValue_read_latency_1333));
        CUST_WRITE_FIELD_BATCH(&table, readLatency1600, config::GetConfigValue(KipConfigValue_read_latency_1600));
        CUST_WRITE_FIELD_BATCH(&table, readLatency1866, config::GetConfigValue(KipConfigValue_read_latency_1866));
        CUST_WRITE_FIELD_BATCH(&table, readLatency2133, config::GetConfigValue(KipConfigValue_read_latency_2133));

        CUST_WRITE_FIELD_BATCH(&table, writeLatency1333, config::GetConfigValue(KipConfigValue_write_latency_1333));
        CUST_WRITE_FIELD_BATCH(&table, writeLatency1600, config::GetConfigValue(KipConfigValue_write_latency_1600));
        CUST_WRITE_FIELD_BATCH(&table, writeLatency1866, config::GetConfigValue(KipConfigValue_write_latency_1866));
        CUST_WRITE_FIELD_BATCH(&table, writeLatency2133, config::GetConfigValue(KipConfigValue_write_latency_2133));

        CUST_WRITE_FIELD_BATCH(&table, mem_burst_read_latency, config::GetConfigValue(KipConfigValue_mem_burst_read_latency));
        CUST_WRITE_FIELD_BATCH(&table, mem_burst_write_latency, config::GetConfigValue(KipConfigValue_mem_burst_write_latency));
        CUST_WRITE_FIELD_BATCH(&table, eristaCpuUV, config::GetConfigValue(KipConfigValue_eristaCpuUV));
        CUST_WRITE_FIELD_BATCH(&table, eristaCpuVmin, config::GetConfigValue(KipConfigValue_eristaCpuVmin));
        CUST_WRITE_FIELD_BATCH(&table, eristaCpuMaxVolt, config::GetConfigValue(KipConfigValue_eristaCpuMaxVolt));
        CUST_WRITE_FIELD_BATCH(&table, eristaCpuUnlock, config::GetConfigValue(KipConfigValue_eristaCpuUnlock));

        CUST_WRITE_FIELD_BATCH(&table, marikoCpuUVLow, config::GetConfigValue(KipConfigValue_marikoCpuUVLow));
        CUST_WRITE_FIELD_BATCH(&table, marikoCpuUVHigh, config::GetConfigValue(KipConfigValue_marikoCpuUVHigh));
        CUST_WRITE_FIELD_BATCH(&table, tableConf, config::GetConfigValue(KipConfigValue_tableConf));
        CUST_WRITE_FIELD_BATCH(&table, marikoCpuLowVmin, config::GetConfigValue(KipConfigValue_marikoCpuLowVmin));
        CUST_WRITE_FIELD_BATCH(&table, marikoCpuHighVmin, config::GetConfigValue(KipConfigValue_marikoCpuHighVmin));
        CUST_WRITE_FIELD_BATCH(&table, marikoCpuMaxVolt, config::GetConfigValue(KipConfigValue_marikoCpuMaxVolt));
        CUST_WRITE_FIELD_BATCH(&table, marikoCpuMaxClock, config::GetConfigValue(KipConfigValue_marikoCpuMaxClock));

        CUST_WRITE_FIELD_BATCH(&table, eristaCpuBoostClock, config::GetConfigValue(KipConfigValue_eristaCpuBoostClock));
        CUST_WRITE_FIELD_BATCH(&table, marikoCpuBoostClock, config::GetConfigValue(KipConfigValue_marikoCpuBoostClock));

        CUST_WRITE_FIELD_BATCH(&table, eristaGpuUV, config::GetConfigValue(KipConfigValue_eristaGpuUV));
        CUST_WRITE_FIELD_BATCH(&table, eristaGpuVmin, config::GetConfigValue(KipConfigValue_eristaGpuVmin));

        CUST_WRITE_FIELD_BATCH(&table, marikoGpuUV, config::GetConfigValue(KipConfigValue_marikoGpuUV));
        CUST_WRITE_FIELD_BATCH(&table, marikoGpuVmin, config::GetConfigValue(KipConfigValue_marikoGpuVmin));
        CUST_WRITE_FIELD_BATCH(&table, marikoGpuVmax, config::GetConfigValue(KipConfigValue_marikoGpuVmax));

        CUST_WRITE_FIELD_BATCH(&table, commonGpuVoltOffset, config::GetConfigValue(KipConfigValue_commonGpuVoltOffset));
        CUST_WRITE_FIELD_BATCH(&table, gpuSpeedo, config::GetConfigValue(KipConfigValue_gpuSpeedo));

        for (int i = 0; i < 24; i++) {
            table.marikoGpuVoltArray[i] = config::GetConfigValue((HocClkConfigValue)(KipConfigValue_g_volt_76800 + i));
        }

        for (int i = 0; i < 27; i++) {
            table.eristaGpuVoltArray[i] = config::GetConfigValue((HocClkConfigValue)(KipConfigValue_g_volt_e_76800 + i));
        }

        CUST_WRITE_FIELD_BATCH(&table, t6_tRTW_fine_tune, config::GetConfigValue(KipConfigValue_t6_tRTW_fine_tune));
        CUST_WRITE_FIELD_BATCH(&table, t7_tWTR_fine_tune, config::GetConfigValue(KipConfigValue_t7_tWTR_fine_tune));

        if (!cust_write_table("sdmc:/atmosphere/kips/hoc.kip", &table)) {
            fileUtils::LogLine("[kip] Failed to write KIP file");
            notification::writeNotification("Horizon OC\nKip write failed");
        }

        HocClkConfigValueList configValues;
        config::GetConfigValues(&configValues);

        configValues.values[KipCrc32] = (u64)crc32::checksum_file("sdmc:/atmosphere/kips/hoc.kip"); // write checksum

        if (config::SetConfigValues(&configValues, false)) {
            fileUtils::LogLine("[kip] KIP data set. CRC32: %ld (Cust Rev %ld)", configValues.values[KipCrc32], configValues.values[KipConfigValue_custRev]);
            for (u64 i = KipConfigValue_hpMode; i < HocClkConfigValue_EnumMax; i++) {
                fileUtils::LogLine("%s: %ld", hocclkFormatConfigValue((HocClkConfigValue)i, false), configValues.values[i]);
            }
        } else {
            fileUtils::LogLine("[kip] Warning: Failed to set config values from KIP");
            notification::writeNotification("Horizon OC\nKip config set failed");
        }
    }

    // I know this is very hacky, but the config system in the sysmodule doesn't really support writing

    void GetKipData()
    {
        FILE* fp;
        if (config::Refresh()) {
            fp = fopen("sdmc:/atmosphere/kips/hoc.kip", "r");

            if (fp == NULL) {
                notification::writeNotification("Horizon OC\nKip opening failed");
                kipAvailable = false;
                return;
            } else {
                kipAvailable = true;
                fclose(fp);
            }

            HocClkConfigValueList configValues;
            config::GetConfigValues(&configValues);

            CustomizeTable table;
            if (!cust_read_and_cache("sdmc:/atmosphere/kips/hoc.kip", &table)) {
                fileUtils::LogLine("[kip] Failed to read KIP file for GetKipData");
                notification::writeNotification("Horizon OC\nKip read failed");
                return;
            }

            // if(cust_get_cust_rev(&table) != CUST_REV) {
            //     notification::writeNotification("Horizon OC\nKip version mismatch\nPlease reinstall Horizon OC");
            //     return;
            // }

            if ((u64)crc32::checksum_file("sdmc:/atmosphere/kips/hoc.kip") != config::GetConfigValue(KipCrc32) && !config::GetConfigValue(HocClkConfigValue_IsFirstLoad)) {
                SetKipData();
                notification::writeNotification("Horizon OC\nKIP has been updated\nPlease reboot your console");
                return;
            }
            if (config::GetConfigValue(HocClkConfigValue_IsFirstLoad) == true) {
                configValues.values[HocClkConfigValue_IsFirstLoad] = (u64)false;
                notification::writeNotification("Horizon OC has been installed");
            }

            configValues.values[KipCrc32] = (u64)crc32::checksum_file("sdmc:/atmosphere/kips/hoc.kip"); // write checksum
            // configValues.values[KipConfigValue_mtcConf] = cust_get_mtc_conf(&table);
            configValues.values[KipConfigValue_hpMode] = cust_get_hp_mode(&table);

            configValues.values[KipConfigValue_commonEmcMemVolt] = cust_get_common_emc_volt(&table);
            configValues.values[KipConfigValue_eristaEmcMaxClock] = cust_get_erista_emc_max(&table);
            configValues.values[KipConfigValue_eristaEmcMaxClock1] = cust_get_erista_emc_max1(&table);
            configValues.values[KipConfigValue_eristaEmcMaxClock2] = cust_get_erista_emc_max2(&table);
            configValues.values[KipConfigValue_marikoEmcMaxClock] = cust_get_mariko_emc_max(&table);
            configValues.values[KipConfigValue_marikoEmcVddqVolt] = cust_get_mariko_emc_vddq(&table);
            configValues.values[KipConfigValue_emcDvbShift] = cust_get_emc_dvb_shift(&table);

            configValues.values[KipConfigValue_t1_tRCD] = cust_get_tRCD(&table);
            configValues.values[KipConfigValue_t2_tRP] = cust_get_tRP(&table);
            configValues.values[KipConfigValue_t3_tRAS] = cust_get_tRAS(&table);
            configValues.values[KipConfigValue_t4_tRRD] = cust_get_tRRD(&table);
            configValues.values[KipConfigValue_t5_tRFC] = cust_get_tRFC(&table);
            configValues.values[KipConfigValue_t6_tRTW] = cust_get_tRTW(&table);
            configValues.values[KipConfigValue_t7_tWTR] = cust_get_tWTR(&table);
            configValues.values[KipConfigValue_t8_tREFI] = cust_get_tREFI(&table);
            configValues.values[KipConfigValue_stepMode] = cust_get_step_mode(&table);

            configValues.values[KipConfigValue_timingEmcTbreak] = cust_get_timing_emc_tbreak(&table);
            configValues.values[KipConfigValue_low_t6_tRTW] = cust_get_low_t6_tRTW(&table);
            configValues.values[KipConfigValue_low_t7_tWTR] = cust_get_low_t7_tWTR(&table);
            configValues.values[KipConfigValue_t2_tRP_cap] = cust_get_tRP_cap(&table);

            configValues.values[KipConfigValue_read_latency_1333] = cust_get_read_latency_1333(&table);
            configValues.values[KipConfigValue_read_latency_1600] = cust_get_read_latency_1600(&table);
            configValues.values[KipConfigValue_read_latency_1866] = cust_get_read_latency_1866(&table);
            configValues.values[KipConfigValue_read_latency_2133] = cust_get_read_latency_2133(&table);

            configValues.values[KipConfigValue_write_latency_1333] = cust_get_write_latency_1333(&table);
            configValues.values[KipConfigValue_write_latency_1600] = cust_get_write_latency_1600(&table);
            configValues.values[KipConfigValue_write_latency_1866] = cust_get_write_latency_1866(&table);
            configValues.values[KipConfigValue_write_latency_2133] = cust_get_write_latency_2133(&table);

            configValues.values[KipConfigValue_mem_burst_read_latency] = cust_get_burst_read_lat(&table);
            configValues.values[KipConfigValue_mem_burst_write_latency] = cust_get_burst_write_lat(&table);

            configValues.values[KipConfigValue_eristaCpuUV] = cust_get_erista_cpu_uv(&table);
            configValues.values[KipConfigValue_eristaCpuVmin] = cust_get_eristaCpuVmin(&table);
            configValues.values[KipConfigValue_eristaCpuMaxVolt] = cust_get_erista_cpu_max_volt(&table);
            configValues.values[KipConfigValue_eristaCpuUnlock] = cust_get_eristaCpuUnlock(&table);

            configValues.values[KipConfigValue_marikoCpuUVLow] = cust_get_mariko_cpu_uv_low(&table);
            configValues.values[KipConfigValue_marikoCpuUVHigh] = cust_get_mariko_cpu_uv_high(&table);
            configValues.values[KipConfigValue_tableConf] = cust_get_table_conf(&table);
            configValues.values[KipConfigValue_marikoCpuLowVmin] = cust_get_mariko_cpu_low_vmin(&table);
            configValues.values[KipConfigValue_marikoCpuHighVmin] = cust_get_mariko_cpu_high_vmin(&table);
            configValues.values[KipConfigValue_marikoCpuMaxVolt] = cust_get_mariko_cpu_max_volt(&table);
            configValues.values[KipConfigValue_marikoCpuMaxClock] = cust_get_marikoCpuMaxClock(&table);
            configValues.values[KipConfigValue_eristaCpuBoostClock] = cust_get_erista_cpu_boost(&table);
            configValues.values[KipConfigValue_marikoCpuBoostClock] = cust_get_mariko_cpu_boost(&table);

            configValues.values[KipConfigValue_eristaGpuUV] = cust_get_erista_gpu_uv(&table);
            configValues.values[KipConfigValue_eristaGpuVmin] = cust_get_erista_gpu_vmin(&table);
            configValues.values[KipConfigValue_marikoGpuUV] = cust_get_mariko_gpu_uv(&table);
            configValues.values[KipConfigValue_marikoGpuVmin] = cust_get_mariko_gpu_vmin(&table);
            configValues.values[KipConfigValue_marikoGpuVmax] = cust_get_mariko_gpu_vmax(&table);
            configValues.values[KipConfigValue_commonGpuVoltOffset] = cust_get_common_gpu_offset(&table);
            configValues.values[KipConfigValue_gpuSpeedo] = board::GetFuseData()->gpuSpeedo; // cust_get_gpu_speedo(&table);

            for (int i = 0; i < 24; i++) {
                configValues.values[KipConfigValue_g_volt_76800 + i] = cust_get_mariko_gpu_volt(&table, i);
            }

            for (int i = 0; i < 27; i++) {
                configValues.values[KipConfigValue_g_volt_e_76800 + i] = cust_get_erista_gpu_volt(&table, i);
            }

            configValues.values[KipConfigValue_t7_tWTR_fine_tune] = cust_get_tWTR_fine_tune(&table);
            configValues.values[KipConfigValue_t6_tRTW_fine_tune] = cust_get_tRTW_fine_tune(&table);



            // if(cust_get_cust_rev(&table) == KIP_CUST_REV)
            //     return;

            if (sizeof(HocClkConfigValueList) <= sizeof(configValues)) {
                if (config::SetConfigValues(&configValues, false)) {
                    fileUtils::LogLine("[kip] KIP loaded. CRC32: %ld (Cust Rev %ld)", configValues.values[KipCrc32], configValues.values[KipConfigValue_custRev]);
                    for (u64 i = KipConfigValue_hpMode; i < HocClkConfigValue_EnumMax; i++) {
                        fileUtils::LogLine("%s: %ld", hocclkFormatConfigValue((HocClkConfigValue)i, false), configValues.values[i]);
                    }
                } else {
                    fileUtils::LogLine("[kip] Warning: Failed to set config values from KIP");
                    notification::writeNotification("Horizon OC\nKip config set failed");
                }
            } else {
                fileUtils::LogLine("[kip] Error: Config value list buffer size mismatch");
                notification::writeNotification("Horizon OC\nConfig Buffer Mismatch");
            }
        } else {
            fileUtils::LogLine("[kip] Config refresh error in GetKipData!");
            notification::writeNotification("Horizon OC\nConfig refresh failed");
        }
    }
}