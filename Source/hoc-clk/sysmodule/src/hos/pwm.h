/*
 * Copyright (c) MasaGratoR
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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Service s;
} PwmChannelSession;

Result pwmInitialize(void);
void pwmExit(void);
Service* pwmGetServiceSession(void);
Result pwmOpenSession2(PwmChannelSession *out, u32 device_code);
Result pwmChannelSessionGetDutyCycle(PwmChannelSession *c, double* out);
void pwmChannelSessionClose(PwmChannelSession *c);

#ifdef __cplusplus
} // extern "C"
#endif