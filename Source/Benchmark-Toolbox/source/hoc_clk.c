#define NX_SERVICE_ASSUME_NON_DOMAIN
#include <assert.h>
#include <switch.h>

#include "hoc_clk.h"
#include <hocclk/clock_manager.h>

#define HOCCLK_SERVICE "hoc:clk"
#define HOCCLK_CMD_GET_CURRENT_CONTEXT 2

static Service g_srv;
static bool g_active = false;

bool hocclk_init(void) {
    if (g_active)
        return true;
    Result rc = smGetService(&g_srv, HOCCLK_SERVICE);
    g_active = R_SUCCEEDED(rc);
    return g_active;
}

void hocclk_exit(void) {
    if (g_active) {
        serviceClose(&g_srv);
        g_active = false;
    }
}

bool hocclk_get(HocClkContext *out) {
    if (!g_active)
        return false;
    Result rc = serviceDispatch(&g_srv, HOCCLK_CMD_GET_CURRENT_CONTEXT, .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
                                .buffers = { { out, sizeof(*out) } }, );
    return R_SUCCEEDED(rc);
}
