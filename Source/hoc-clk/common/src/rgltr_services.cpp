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

#include <switch.h>
#include "rgltr.h"
#include "rgltr_services.h"  // for extern Service g_rgltrSrv, etc.

// Global service handle
Service g_rgltrSrv;

Result rgltrInitialize(void) {
    if (hosversionBefore(8, 0, 0)) {
        return MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer);
    }
    return smGetService(&g_rgltrSrv, "rgltr");
}

void rgltrExit(void) {
    serviceClose(&g_rgltrSrv);
}

Result rgltrOpenSession(RgltrSession* session_out, PowerDomainId module_id) {
    const u32 in = (u32)module_id;
    return serviceDispatchIn(
        &g_rgltrSrv,
        0,
        in,
        .out_num_objects = 1,
        .out_objects     = &session_out->s
    );
}

Result rgltrGetVoltage(RgltrSession* session, u32* out_volt) {
    u32 temp = 0;
    Result rc = serviceDispatchOut(&session->s, 4, temp);
    if (R_SUCCEEDED(rc)) {
        *out_volt = temp;
    }
    return rc;
}

Result rgltrRequestVoltage(RgltrSession* session, u32 microvolt) {
    return serviceDispatchIn(&session->s, 5, microvolt);
}

Result rgltrCancelVoltageRequest(RgltrSession* session) {
    return serviceDispatch(&session->s, 6);
}

void rgltrCloseSession(RgltrSession* session) {
    serviceClose(&session->s);
}
