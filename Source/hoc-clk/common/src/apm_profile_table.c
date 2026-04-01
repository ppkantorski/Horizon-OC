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


#include <sysclk/apm.h>

SysClkApmConfiguration sysclk_g_apm_configurations[] = {
    {0x00010000, 1020000000, 384000000, 1600000000},
    {0x00010001, 1020000000, 768000000, 1600000000},
    {0x00010002, 1224000000, 691200000, 1600000000},
    {0x00020000, 1020000000, 230400000, 1600000000},
    {0x00020001, 1020000000, 307200000, 1600000000},
    {0x00020002, 1224000000, 230400000, 1600000000},
    {0x00020003, 1020000000, 307200000, 1331200000},
    {0x00020004, 1020000000, 384000000, 1331200000},
    {0x00020005, 1020000000, 307200000, 1065600000},
    {0x00020006, 1020000000, 384000000, 1065600000},
    {0x92220007, 1020000000, 460800000, 1600000000},
    {0x92220008, 1020000000, 460800000, 1331200000},
    {0x92220009, 1785000000,  76800000, 1600000000},
    {0x9222000A, 1785000000,  76800000, 1331200000},
    {0x9222000B, 1020000000,  76800000, 1600000000},
    {0x9222000C, 1020000000,  76800000, 1331200000},
    {0, 0, 0, 0},
};

