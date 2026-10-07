/**
 * @file    FPGA_CONFIG.c
 * @brief   Watch the FPGA's configuration status.  See FPGA_CONFIG.h.
 */
#include "FPGA_CONFIG.h"

#include <stddef.h>

void FpgaConfig_Init(FpgaConfig_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    cfg->state = FPGA_CFG_UNKNOWN;
    cfg->since_ms = 0u;
    cfg->ever_done = false;
}

void FpgaConfig_Sample(FpgaConfig_t *cfg, bool line_high, uint32_t now_ms)
{
    if (cfg == NULL) {
        return;
    }
    if (cfg->state == FPGA_CFG_UNKNOWN) {
        cfg->since_ms = now_ms;                 /* start the clock on the first sample */
    }

    if (line_high) {
        cfg->state = FPGA_CFG_DONE;
        cfg->ever_done = true;
        return;
    }

    if (cfg->ever_done) {
        return;                                 /* configured once; a later low is not a regression */
    }

    /* still low: in progress until the timeout, then a failure worth reporting */
    if ((uint32_t)(now_ms - cfg->since_ms) >= FPGA_CFG_TIMEOUT_MS) {
        cfg->state = FPGA_CFG_FAILED;
    } else if (cfg->state != FPGA_CFG_FAILED) {
        cfg->state = FPGA_CFG_IN_PROGRESS;
    }
}

bool FpgaConfig_IsConfigured(const FpgaConfig_t *cfg)
{
    return cfg != NULL && cfg->ever_done;
}

bool FpgaConfig_HasFailed(const FpgaConfig_t *cfg)
{
    return cfg != NULL && cfg->state == FPGA_CFG_FAILED;
}

const char *FpgaConfig_StateName(FpgaConfigState_t state)
{
    switch (state) {
    case FPGA_CFG_IN_PROGRESS: return "configuring";
    case FPGA_CFG_DONE:        return "done";
    case FPGA_CFG_FAILED:      return "FAILED";
    default:                   return "unknown";
    }
}
