/**
 * @file    FPGA_CONFIG.h
 * @brief   Watch the FPGA's configuration status (issue #13).
 *
 * The issue's second finding was that a failed or partial FPGA configuration is invisible to the
 * firmware that supervises the radar: the configuration pins terminated in pull resistors only.
 *
 * That is no longer true - the main board netlist carries `FPGA_DONE` from the FPGA's DONE_0 and
 * INIT_B_0 (U42) to **U2.PC4** - but the firmware never read it, so the acceptance criterion
 * ("force a failed configuration: the MCU must report it") was unmet in the code even though the
 * wire existed.
 *
 * The line carries both signals, which is what makes the test possible: the FPGA drives DONE, the
 * MCU may drive INIT_B, and the net reads low while either the FPGA is unconfigured or the MCU is
 * holding INIT_B.  A pull resistor defines the idle level.
 *
 * DONE is a *level*, not a pulse, so the state is simply "has this line been high since reset".
 */
#ifndef FPGA_CONFIG_H
#define FPGA_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/** How long the line may stay low after reset before the configuration counts as failed. */
#define FPGA_CFG_TIMEOUT_MS 2000u

typedef enum {
    FPGA_CFG_UNKNOWN = 0,   /**< not sampled yet */
    FPGA_CFG_IN_PROGRESS,   /**< line low, still inside the timeout */
    FPGA_CFG_DONE,          /**< the line went high: the FPGA reports configured */
    FPGA_CFG_FAILED         /**< line still low after the timeout */
} FpgaConfigState_t;

typedef struct {
    FpgaConfigState_t state;
    uint32_t          since_ms;     /**< when sampling began */
    bool              ever_done;    /**< sticky: it configured at least once */
} FpgaConfig_t;

void FpgaConfig_Init(FpgaConfig_t *cfg);

/**
 * Feed the line's level, with the current tick.
 * Once DONE, later lows do not clear it - a configured FPGA does not unconfigure itself, and a
 * transient low during a reconfiguration is not a failure of the design.
 */
void FpgaConfig_Sample(FpgaConfig_t *cfg, bool line_high, uint32_t now_ms);

bool FpgaConfig_IsConfigured(const FpgaConfig_t *cfg);
bool FpgaConfig_HasFailed(const FpgaConfig_t *cfg);

/** A one-line description for telemetry. */
const char *FpgaConfig_StateName(FpgaConfigState_t state);

#endif /* FPGA_CONFIG_H */
