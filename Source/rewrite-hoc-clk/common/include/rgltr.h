/*
 * Copyright (c) ppkantorski (bord2death)
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
#include "pcv_types.h"

typedef struct {
    Service s;
} RgltrSession;

Result rgltrInitialize(void);

void rgltrExit(void);

Service* rgltrGetServiceSession(void);

Result rgltrOpenSession(RgltrSession* session_out, PowerDomainId module_id);
void rgltrCloseSession(RgltrSession* session);
Result rgltrGetVoltage(RgltrSession* session, u32 *out_volt);
Result rgltrGetPowerModuleNumLimit(u32 *out);
Result rgltrGetVoltageEnabled(RgltrSession* session, u32 *out);
Result rgltrRequestVoltage(RgltrSession* session, u32 microvolt);
Result rgltrCancelVoltageRequest(RgltrSession* session);