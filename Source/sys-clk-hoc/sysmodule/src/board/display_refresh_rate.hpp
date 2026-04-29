/*
 * Copyright (c) Souldbminer, based on reasearch by MasaGratoR and Cooler3D
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

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
namespace display {
    typedef struct {
        uint16_t hFrontPorch;
        uint8_t hSyncWidth;
        uint8_t hBackPorch;
        uint8_t vFrontPorch;
        uint8_t vSyncWidth;
        uint8_t vBackPorch;
        uint8_t VIC;
        uint32_t pixelClock_kHz;
    } DockedTimings;

    typedef struct {
        uint8_t hSyncWidth;
        uint16_t hFrontPorch;
        uint8_t hBackPorch;
        uint8_t vSyncWidth;
        uint16_t vFrontPorch;
        uint8_t vBackPorch;
        uint32_t pixelClock_kHz;
    } HandheldTimings;

    typedef struct {
        uint8_t min;
        uint8_t max;
    } MinMaxRefreshRate;

    typedef struct {
        uint32_t unk0;
        uint32_t hActive;
        uint32_t vActive;
        uint32_t hSyncWidth;
        uint32_t vSyncWidth;
        uint32_t hFrontPorch;
        uint32_t vFrontPorch;
        uint32_t hBackPorch;
        uint32_t vBackPorch;
        uint32_t pclkKHz;
        uint32_t bitsPerPixel;
        uint32_t vmode;
        uint32_t sync;
        uint32_t unk1;
        uint32_t reserved;
    } NvdcMode2;

    typedef struct {
        NvdcMode2 modes[201];
        uint32_t num_modes;
    } NvdcModeDB2;

    typedef struct {
        unsigned int PLLD_DIVM: 8;
        unsigned int reserved_1: 3;
        unsigned int PLLD_DIVN: 8;
        unsigned int reserved_2: 1;
        unsigned int PLLD_DIVP: 3;
        unsigned int CSI_CLK_SRC: 1;
        unsigned int reserved_3: 1;
        unsigned int PLL_D: 1;
        unsigned int reserved_4: 1;
        unsigned int PLLD_LOCK: 1;
        unsigned int reserved_5: 1;
        unsigned int PLLD_REF_DIS: 1;
        unsigned int PLLD_ENABLE: 1;
        unsigned int PLLD_BYPASS: 1;
    } PLLD_BASE;

    typedef struct {
        signed int PLLD_SDM_DIN: 16;
        unsigned int PLLD_EN_SDM: 1;
        unsigned int PLLD_LOCK_OVERRIDE: 1;
        unsigned int PLLD_EN_LCKDET: 1;
        unsigned int PLLD_FREQLOCK: 1;
        unsigned int PLLD_IDDQ: 1;
        unsigned int PLLD_ENABLE_CLK: 1;
        unsigned int PLLD_KVCO: 1;
        unsigned int PLLD_KCP: 2;
        unsigned int PLLD_PTS: 2;
        unsigned int PLLD_LDPULSE_ADJ: 3;
        unsigned int reserved: 2;
    } PLLD_MISC;

    typedef struct {
        uint64_t clkVirtAddr;
        uint64_t dsiVirtAddr;
        bool isDocked;
        bool isLite;
        bool isRetroSUPER;
        bool isPossiblySpoofedRetro;
        bool dontForce60InDocked;
        bool matchLowestDocked;
        bool displaySync;
        bool displaySyncOutOfFocus60;
        bool displaySyncDocked;
        bool displaySyncDockedOutOfFocus60;
    } DisplayRefreshConfig;
    bool Initialize(const DisplayRefreshConfig* config);
    void SetDockedState(bool isDocked);
    bool SetRate(uint32_t new_refreshRate);
    bool GetRate(uint32_t* out_refreshRate, bool internal);
    uint8_t GetDockedHighestAllowed(void);
    void CorrectOledGamma(uint32_t refresh_rate);
    void SetAllowedDockedRatesIPC(uint32_t refreshRates, bool is720p);
    void Shutdown(void);
}