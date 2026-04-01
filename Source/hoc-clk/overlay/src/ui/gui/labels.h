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
#include <map>
#include <cstdint>
#include <string>

extern std::map<uint32_t, std::string> cpu_freq_label_m;
extern std::map<uint32_t, std::string> cpu_freq_label_m_uv;
extern std::map<uint32_t, std::string> cpu_freq_label_e;
extern std::map<uint32_t, std::string> cpu_freq_label_e_uv;
extern std::map<uint32_t, std::string> gpu_freq_label_m;
extern std::map<uint32_t, std::string> gpu_freq_label_m_slt;
extern std::map<uint32_t, std::string> gpu_freq_label_m_hiopt;
extern std::map<uint32_t, std::string> gpu_freq_label_e;
extern std::map<uint32_t, std::string> gpu_freq_label_e_uv;

extern std::map<uint32_t, std::string>* marikoUV[3];
extern std::map<uint32_t, std::string>* eristaUV[3];