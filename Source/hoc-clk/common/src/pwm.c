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

#define NX_SERVICE_ASSUME_NON_DOMAIN
#include <switch.h>
#include "service_guard.h"
#include "pwm.h"

static Service g_pwmSrv;

NX_GENERATE_SERVICE_GUARD(pwm);

Result _pwmInitialize(void) {
    return smGetService(&g_pwmSrv, "pwm");
}

void _pwmCleanup(void) {
    serviceClose(&g_pwmSrv);
}

Service* pwmGetServiceSession(void) {
    return &g_pwmSrv;
}

Result pwmOpenSession2(PwmChannelSession *out, u32 device_code) {
    return serviceDispatchIn(&g_pwmSrv, 2, device_code,
        .out_num_objects = 1,
        .out_objects = &out->s,
    );
}

Result pwmChannelSessionGetDutyCycle(PwmChannelSession *c, double* out) {
    return serviceDispatchOut(&c->s, 7, *out);
}

void pwmChannelSessionClose(PwmChannelSession *controller) {
    serviceClose(&controller->s);
}