/*
 *
 * TEGRA AOTAG (Always-On Thermal Alert Generator) driver.
 *
 * Copyright (c) 2014 - 2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * Copyright (c) 2026, Souldbminer
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#pragma once

namespace tsensor {

    #define MASK(start, end)	\
	(((0xFFFFFFFF)<<start) & (u32)(((u64)1<<(end+1))-1))

    #define MAX_THRESHOLD_TEMP	(127000)
    #define MIN_THRESHOLD_TEMP	(-127000)

    /*
    * Register definitions
    */
    #define TSENSOR_COMMON_FUSE_ADDR	(0x280)

    // NVIDIA driver uses 1D4 here but its incorrect.
    // I guess that's what happens when you vibecode your AOTAG driver (jk)
    #define AOTAG_FUSE_ADDR			(0x2D4)

    #define PMC_R_OBS_AOTAG			(0x017)
    #define PMC_R_OBS_AOTAG_CAPTURE		(0x017)
    #define PMC_RST_STATUS			(0x1B4)
    #define PMC_DIRECT_THERMTRIP_CFG	(0x474)

    #define PMC_AOTAG_CFG			(0x484)
    #define CFG_DISABLE_CLK_POS		(0)
    #define CFG_SW_TRIGGER_POS		(2)
    #define CFG_USE_EXT_TRIGGER_POS		(3)
    #define CFG_ENABLE_SW_DEBUG_POS		(4)
    #define CFG_TAG_EN_POS			(5)
    #define CFG_THERMTRIP_EN_POS		(6)

    #define PMC_AOTAG_THRESH1_CFG		(0x488)
    #define PMC_AOTAG_THRESH2_CFG		(0x48C)
    #define PMC_AOTAG_THRESH3_CFG		(0x490)
    #define THRESH3_CFG_POS_START		(0)
    #define THRESH3_CFG_POS_END		(14)
    #define THRESH3_CFG_MASK		(MASK(THRESH3_CFG_POS_START,\
                            THRESH3_CFG_POS_END))
    #define PMC_AOTAG_STATUS		(0x494)
    #define PMC_AOTAG_SECURITY		(0x498)

    #define PMC_TSENSOR_CONFIG0		(0x49C)
    #define CONFIG0_STOP_POS		(0)
    #define CONFIG0_RO_SEL_POS		(1)
    #define CONFIG0_STATUS_CLR_POS		(5)
    #define CONFIG0_TALL_POS_START		(8)
    #define CONFIG0_TALL_POS_END		(27)
    #define CONFIG0_TALL_SIZE		(CONFIG0_TALL_POS_END - \
                        CONFIG0_TALL_POS + 1)
    #define CONFIG0_TALL_MASK		(MASK(CONFIG0_TALL_POS_START, \
                            CONFIG0_TALL_POS_END))

    #define PMC_TSENSOR_CONFIG1		(0x4A0)
    #define CONFIG1_TEMP_ENABLE_POS		(31)
    #define CONFIG1_TEN_COUNT_POS_START	(24)
    #define CONFIG1_TEN_COUNT_POS_END	(29)
    #define CONFIG1_TEN_COUNT_MASK		(MASK(CONFIG1_TEN_COUNT_POS_START, \
                            CONFIG1_TEN_COUNT_POS_END))
    #define CONFIG1_TIDDQ_EN_POS_START	(15)
    #define CONFIG1_TIDDQ_EN_POS_END	(20)
    #define CONFIG1_TIDDQ_EN_MASK		(MASK(CONFIG1_TIDDQ_EN_POS_START, \
                            CONFIG1_TIDDQ_EN_POS_END))
    #define CONFIG1_TSAMPLE_POS_START	(0)
    #define CONFIG1_TSAMPLE_POS_END		(9)
    #define CONFIG1_TSAMPLE_MASK		(MASK(CONFIG1_TSAMPLE_POS_START, \
                            CONFIG1_TSAMPLE_POS_END))

    #define PMC_TSENSOR_CONFIG2		(0x4A4)
    #define CONFIG2_THERM_A_POS_START	(16)
    #define CONFIG2_THERM_A_POS_END		(31)
    #define CONFIG2_THERM_A_MASK		(MASK(CONFIG2_THERM_A_POS_START, \
                            CONFIG2_THERM_A_POS_END))

    #define CONFIG2_THERM_B_POS_START	(0)
    #define CONFIG2_THERM_B_POS_END		(15)
    #define CONFIG2_THERM_B_MASK		(MASK(CONFIG2_THERM_B_POS_START, \
                            CONFIG2_THERM_B_POS_END))

    #define PMC_TSENSOR_STATUS0		(0x4A8)
    #define STATUS0_CAPTURE_VALID_POS	(31)
    #define STATUS0_CAPTURE_POS_START	(0)
    #define STATUS0_CAPTURE_POS_END		(15)
    #define STATUS0_CAPTURE_MASK		(MASK(STATUS0_CAPTURE_POS_START, \
                        STATUS0_CAPTURE_POS_END))

    #define PMC_TSENSOR_STATUS1		(0x4AC)
    #define STATUS1_TEMP_POS		(31)
    #define STATUS1_TEMP_POS_START		(0)
    #define STATUS1_TEMP_POS_END		(15)
    #define STATUS1_TEMP_MASK		(MASK(STATUS1_TEMP_POS_START, \
                        STATUS1_TEMP_POS_END))

    #define STATUS1_TEMP_VALID_POS_START	(31)
    #define STATUS1_TEMP_VALID_POS_END	(31)
    #define STATUS1_TEMP_VALID_MASK		(MASK(STATUS1_TEMP_VALID_POS_START, \
                        STATUS1_TEMP_VALID_POS_END))

    #define STATUS1_TEMP_ABS_POS_START	(8)
    #define STATUS1_TEMP_ABS_POS_END	(15)
    #define STATUS1_TEMP_ABS_MASK		(MASK(STATUS1_TEMP_ABS_POS_START, \
                        STATUS1_TEMP_ABS_POS_END))

    #define STATUS1_TEMP_FRAC_POS_START	(7)
    #define STATUS1_TEMP_FRAC_POS_END	(7)
    #define STATUS1_TEMP_FRAC_MASK		(MASK(STATUS1_TEMP_FRAC_POS_START, \
                        STATUS1_TEMP_FRAC_POS_END))

    #define STATUS1_TEMP_SIGN_POS_START	(0)
    #define STATUS1_TEMP_SIGN_POS_END	(0)
    #define STATUS1_TEMP_SIGN_MASK		(MASK(STATUS1_TEMP_SIGN_POS_START, \
                        STATUS1_TEMP_SIGN_POS_END))

    #define PMC_TSENSOR_STATUS2		(0x4B0)
    #define PMC_TSENSOR_PDIV0		(0x4B4)
    #define TSENSOR_PDIV_POS_START		(12)
    #define TSENSOR_PDIV_POS_END		(15)
    #define TSENSOR_PDIV_MASK		(MASK(TSENSOR_PDIV_POS_START, \
                            TSENSOR_PDIV_POS_END))

    #define PMC_AOTAG_INTR_EN		(0x4B8)
    #define PMC_AOTAG_INTR_DIS		(0x4BC)

    #define AOTAG_FUSE_CALIB_CP_POS_START	(0)
    #define AOTAG_FUSE_CALIB_CP_POS_END	(12)
    #define AOTAG_FUSE_CALIB_CP_MASK	(MASK(AOTAG_FUSE_CALIB_CP_POS_START, \
                            AOTAG_FUSE_CALIB_CP_POS_END))

    #define AOTAG_FUSE_CALIB_FT_POS_START	(13)
    #define AOTAG_FUSE_CALIB_FT_POS_END	(25)
    #define AOTAG_FUSE_CALIB_FT_MASK	(MASK(AOTAG_FUSE_CALIB_FT_POS_START, \
                            AOTAG_FUSE_CALIB_FT_POS_END))

    void InitializeAotag(bool isMariko);
    s32 ReadAotag();
    bool IsInitialized();
}
