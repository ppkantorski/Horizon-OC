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

#pragma once
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "config.hpp"
#include "file_utils.hpp"
#include <notification.h>
#include <crc32.h>

#pragma pack(push, 1)

namespace kip {
    extern bool kipAvailable;

    typedef struct {
        u8  cust[4];
        u32 custRev;
        u32 hocVersion;
        u32 hpMode;
        u32 commonEmcMemVolt;
        u32 eristaEmcMaxClock;
        u32 stepMode;
        u32 marikoEmcMaxClock;
        u32 marikoEmcVddqVolt;
        s32 emcDvbShift;
        u32 marikoSocVmax;

        // advanced config
        u32 t1_tRCD;
        u32 t2_tRP;
        u32 t3_tRAS;
        u32 t4_tRRD;
        u32 t5_tRFC;
        u32 t6_tRTW;
        u32 t7_tWTR;
        u32 t8_tREFI;

        u32 timingEmcTbreak;
        u32 low_t1_tRCD;
        u32 low_t2_tRP;
        u32 low_t3_tRAS;
        u32 low_t4_tRRD;
        u32 low_t5_tRFC;
        u32 low_t6_tRTW;
        u32 low_t7_tWTR;
        u32 low_t8_tREFI;

        /* These latencies are arrays in loader, but it's easier to handle it this way in the configurator. */
        u32 readLatency1333, readLatency1600, readLatency1866, readLatency2133;
        u32 writeLatency1333, writeLatency1600, writeLatency1866, writeLatency2133;

        u32 eristaCpuUV;
        u32 eristaCpuVmin;
        u32 eristaCpuMaxVolt;
        u32 eristaCpuUnlock;

        u32 marikoCpuUVLow;
        u32 marikoCpuUVHigh;
        u32 tableConf;
        u32 marikoCpuLowVmin;
        u32 marikoCpuHighVmin;
        u32 marikoCpuMaxVolt;
        u32 marikoCpuMaxClock;

        u32 eristaCpuBoostClock;
        u32 marikoCpuBoostClock;

        u32 eristaGpuUV;
        u32 eristaGpuVmin;

        u32 marikoGpuUV;
        u32 marikoGpuVmin;
        u32 marikoGpuVmax;

        u32 commonGpuVoltOffset;

        u32 eristaGpuVoltArray[27];
        u32 marikoGpuVoltArray[24];
        s32 marikoSocVoltArray[28];

        u32 t6_tRTW_fine_tune;
        u32 t7_tWTR_fine_tune;

        u32 reserved[60];
    } CustomizeTable;

    #pragma pack(pop)

    #define CUST_MAGIC "CUST"
    #define CUST_MAGIC_LEN 4

    typedef struct {
        FILE* file;
        long offset;
        CustomizeTable cached_table;
        bool has_cache;
    } CustHandle;

    static inline bool cust_find_offset(FILE* f, long* out_offset) {
        u8 buf[512];
        long pos = 0;
        fseek(f, 0, SEEK_SET);

        while (1) {
            size_t r = fread(buf, 1, sizeof(buf), f);
            if (r < CUST_MAGIC_LEN) break;

            for (size_t i = 0; i <= r - CUST_MAGIC_LEN; i++) {
                if (memcmp(&buf[i], CUST_MAGIC, CUST_MAGIC_LEN) == 0) {
                    *out_offset = pos + (long)i;
                    return true;
                }
            }
            pos += (long)(r - (CUST_MAGIC_LEN - 1));
            fseek(f, pos, SEEK_SET);
        }
        return false;
    }

    static inline bool cust_read_table(const char* path, CustomizeTable* out) {
        FILE* f = fopen(path, "rb");
        if (!f) return false;

        long off;
        if (!cust_find_offset(f, &off)) {
            fclose(f);
            return false;
        }

        fseek(f, 0, SEEK_END);
        long size = ftell(f);

        if (off + (long)sizeof(CustomizeTable) > size) {
            fclose(f);
            return false;
        }

        fseek(f, off, SEEK_SET);
        bool ok = fread(out, 1, sizeof(CustomizeTable), f) == sizeof(CustomizeTable);
        fclose(f);

        return ok && memcmp(out->cust, CUST_MAGIC, CUST_MAGIC_LEN) == 0;
    }

    static inline bool cust_read_and_cache(const char* path, CustomizeTable* out) {
        return cust_read_table(path, out);
    }

    // NOTE: The KIP write path (cust_write_table + cust_set_* + CUST_WRITE_FIELD)
    // was removed. sys-clk-hoc is read-only: the KIP is managed externally by
    // the HOC Toolkit, which is the single source of truth. Only the read-side
    // accessors below are needed, and only a handful are actually consumed.

    static inline u32 cust_get_field(const CustomizeTable* t, u32 offset) {
        if (!t) return 0;
        return *(u32*)((u8*)t + offset);
    }

    #define CUST_GET_FIELD(table, field) ((table) ? (table)->field : 0)

    static inline u32 cust_get_cust_rev(const CustomizeTable* t) { return CUST_GET_FIELD(t, custRev); }
    // static inline u32 cust_get_mtc_conf(const CustomizeTable* t) { return CUST_GET_FIELD(t, mtcConf); }
    static inline u32 cust_get_hp_mode(const CustomizeTable* t) { return CUST_GET_FIELD(t, hpMode); }

    static inline u32 cust_get_common_emc_volt(const CustomizeTable* t) { return CUST_GET_FIELD(t, commonEmcMemVolt); }
    static inline u32 cust_get_erista_emc_max(const CustomizeTable* t) { return CUST_GET_FIELD(t, eristaEmcMaxClock); }
    static inline u32 cust_get_step_mode(const CustomizeTable* t) { return CUST_GET_FIELD(t, stepMode); }
    static inline u32 cust_get_mariko_emc_max(const CustomizeTable* t) { return CUST_GET_FIELD(t, marikoEmcMaxClock); }
    static inline u32 cust_get_mariko_emc_vddq(const CustomizeTable* t) { return CUST_GET_FIELD(t, marikoEmcVddqVolt); }
    static inline s32 cust_get_emc_dvb_shift(const CustomizeTable* t) { return CUST_GET_FIELD(t, emcDvbShift); }

    static inline u32 cust_get_tRCD(const CustomizeTable* t) { return CUST_GET_FIELD(t, t1_tRCD); }
    static inline u32 cust_get_tRP(const CustomizeTable* t) { return CUST_GET_FIELD(t, t2_tRP); }
    static inline u32 cust_get_tRAS(const CustomizeTable* t) { return CUST_GET_FIELD(t, t3_tRAS); }
    static inline u32 cust_get_tRRD(const CustomizeTable* t) { return CUST_GET_FIELD(t, t4_tRRD); }
    static inline u32 cust_get_tRFC(const CustomizeTable* t) { return CUST_GET_FIELD(t, t5_tRFC); }
    static inline u32 cust_get_tRTW(const CustomizeTable* t) { return CUST_GET_FIELD(t, t6_tRTW); }
    static inline u32 cust_get_tWTR(const CustomizeTable* t) { return CUST_GET_FIELD(t, t7_tWTR); }
    static inline u32 cust_get_tREFI(const CustomizeTable* t) { return CUST_GET_FIELD(t, t8_tREFI); }
    static inline u32 cust_get_timing_emc_tbreak(const CustomizeTable* t) { return CUST_GET_FIELD(t, timingEmcTbreak); }
    static inline u32 cust_get_low_t1_tRCD(const CustomizeTable* t) { return CUST_GET_FIELD(t, low_t1_tRCD); }
    static inline u32 cust_get_low_t2_tRP(const CustomizeTable* t) { return CUST_GET_FIELD(t, low_t2_tRP); }
    static inline u32 cust_get_low_t3_tRAS(const CustomizeTable* t) { return CUST_GET_FIELD(t, low_t3_tRAS); }
    static inline u32 cust_get_low_t4_tRRD(const CustomizeTable* t) { return CUST_GET_FIELD(t, low_t4_tRRD); }
    static inline u32 cust_get_low_t5_tRFC(const CustomizeTable* t) { return CUST_GET_FIELD(t, low_t5_tRFC); }
    static inline u32 cust_get_low_t6_tRTW(const CustomizeTable* t) { return CUST_GET_FIELD(t, low_t6_tRTW); }
    static inline u32 cust_get_low_t7_tWTR(const CustomizeTable* t) { return CUST_GET_FIELD(t, low_t7_tWTR); }
    static inline u32 cust_get_low_t8_tREFI(const CustomizeTable* t) { return CUST_GET_FIELD(t, low_t8_tREFI); }
    static inline u32 cust_get_tRTW_fine_tune(const CustomizeTable* t) { return CUST_GET_FIELD(t, t6_tRTW_fine_tune); }
    static inline u32 cust_get_tWTR_fine_tune(const CustomizeTable* t) { return CUST_GET_FIELD(t, t7_tWTR_fine_tune); }

    static inline u32 cust_get_read_latency_1333(const CustomizeTable* t) { return CUST_GET_FIELD(t, readLatency1333); }
    static inline u32 cust_get_read_latency_1600(const CustomizeTable* t) { return CUST_GET_FIELD(t, readLatency1600); }
    static inline u32 cust_get_read_latency_1866(const CustomizeTable* t) { return CUST_GET_FIELD(t, readLatency1866); }
    static inline u32 cust_get_read_latency_2133(const CustomizeTable* t) { return CUST_GET_FIELD(t, readLatency2133); }

    static inline u32 cust_get_write_latency_1333(const CustomizeTable* t) { return CUST_GET_FIELD(t, writeLatency1333); }
    static inline u32 cust_get_write_latency_1600(const CustomizeTable* t) { return CUST_GET_FIELD(t, writeLatency1600); }
    static inline u32 cust_get_write_latency_1866(const CustomizeTable* t) { return CUST_GET_FIELD(t, writeLatency1866); }
    static inline u32 cust_get_write_latency_2133(const CustomizeTable* t) { return CUST_GET_FIELD(t, writeLatency2133); }

    static inline u32 cust_get_erista_cpu_uv(const CustomizeTable* t) { return CUST_GET_FIELD(t, eristaCpuUV); }
    static inline u32 cust_get_eristaCpuVmin(const CustomizeTable* t) { return CUST_GET_FIELD(t, eristaCpuVmin); }
    static inline u32 cust_get_erista_cpu_max_volt(const CustomizeTable* t) { return CUST_GET_FIELD(t, eristaCpuMaxVolt); }
    static inline u32 cust_get_eristaCpuUnlock(const CustomizeTable* t) { return CUST_GET_FIELD(t, eristaCpuUnlock); }

    static inline u32 cust_get_mariko_cpu_uv_low(const CustomizeTable* t) { return CUST_GET_FIELD(t, marikoCpuUVLow); }
    static inline u32 cust_get_mariko_cpu_uv_high(const CustomizeTable* t) { return CUST_GET_FIELD(t, marikoCpuUVHigh); }
    static inline u32 cust_get_mariko_cpu_low_vmin(const CustomizeTable* t) { return CUST_GET_FIELD(t, marikoCpuLowVmin); }
    static inline u32 cust_get_mariko_cpu_high_vmin(const CustomizeTable* t) { return CUST_GET_FIELD(t, marikoCpuHighVmin); }
    static inline u32 cust_get_mariko_cpu_max_volt(const CustomizeTable* t) { return CUST_GET_FIELD(t, marikoCpuMaxVolt); }
    static inline u32 cust_get_erista_cpu_boost(const CustomizeTable* t) { return CUST_GET_FIELD(t, eristaCpuBoostClock); }
    static inline u32 cust_get_mariko_cpu_boost(const CustomizeTable* t) { return CUST_GET_FIELD(t, marikoCpuBoostClock); }
    static inline u32 cust_get_table_conf(const CustomizeTable* t) { return CUST_GET_FIELD(t, tableConf); }

    static inline u32 cust_get_erista_gpu_uv(const CustomizeTable* t) { return CUST_GET_FIELD(t, eristaGpuUV); }
    static inline u32 cust_get_erista_gpu_vmin(const CustomizeTable* t) { return CUST_GET_FIELD(t, eristaGpuVmin); }
    static inline u32 cust_get_mariko_gpu_uv(const CustomizeTable* t) { return CUST_GET_FIELD(t, marikoGpuUV); }
    static inline u32 cust_get_mariko_gpu_vmin(const CustomizeTable* t) { return CUST_GET_FIELD(t, marikoGpuVmin); }
    static inline u32 cust_get_mariko_gpu_vmax(const CustomizeTable* t) { return CUST_GET_FIELD(t, marikoGpuVmax); }
    static inline u32 cust_get_common_gpu_offset(const CustomizeTable* t) { return CUST_GET_FIELD(t, commonGpuVoltOffset); }
    static inline u32 cust_get_marikoCpuMaxClock(const CustomizeTable* t) { return CUST_GET_FIELD(t, marikoCpuMaxClock); }
    static inline u32 cust_get_marikoSocVmax(const CustomizeTable* t) { return CUST_GET_FIELD(t, marikoSocVmax); }

    static inline u32 cust_get_erista_gpu_volt(const CustomizeTable* t, int idx) {
        if (!t || idx < 0 || idx >= 27) return 0;
        return t->eristaGpuVoltArray[idx];
    }

    static inline u32 cust_get_mariko_gpu_volt(const CustomizeTable* t, int idx) {
        if (!t || idx < 0 || idx >= 24) return 0;
        return t->marikoGpuVoltArray[idx];
    }

    static inline u32 cust_get_mariko_soc_volt(const CustomizeTable* t, int idx) {
        if (!t || idx < 0 || idx >= 28) return 0;
        return (u32)t->marikoSocVoltArray[idx];
    }

    // (per-frequency KIP volt set/get wrappers removed - unused in read-only build)

    void SetKipData();
    void GetKipData();
}