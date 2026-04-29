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

#include <switch.h>  // for Service, Result, hosversionBefore(), smGetService(), serviceClose(), etc.
#include "rgltr.h"   // for RgltrSession, PowerDomainId, etc.

extern Service g_rgltrSrv;

Result rgltrInitialize(void);
void   rgltrExit(void);

Result rgltrOpenSession(RgltrSession* session_out, PowerDomainId module_id);

Result rgltrGetVoltage(RgltrSession* session, u32* out_volt);

void   rgltrCloseSession(RgltrSession* session);
