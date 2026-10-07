/**
 * @file    PA_BIAS_CAL.h
 * @brief   The IDQ calibration loop, device-parameterised and testable (issue #21).
 *
 * The loop in main.cpp was written for the QPA2962 and carried three of that device's numbers
 * inline: a start code of 126 (the old boot value), a step of 4 DAC codes, and a stopping window of
 * an absolute 0.2 A.  Against the QPA1010's 0.600 A target the window is +/-33 % rather than the old
 * +/-12 %, and 4 codes is 0.126 V of gate - roughly 190 mA of drain current on this device, which is
 * comparable to the whole window.  A loop can walk past a window that narrow and settle on the
 * floor instead.
 *
 * So the loop lives here, driven entirely by the device's own constants, and it is a bisection
 * rather than a linear walk: the transfer curve is monotonic (a lower code is a less negative gate
 * and therefore more drain current), so bisecting the code finds the answer in about seven steps
 * instead of up to fifty, and it cannot step over the target.
 *
 * The IO is callbacks, so the same code is exercised by a model device in the test and by the real
 * DAC and ADC in the firmware.
 */
#ifndef PA_BIAS_CAL_H
#define PA_BIAS_CAL_H

#include <stdbool.h>
#include <stdint.h>

/** Stop when within this fraction of the target.  10 % of 600 mA is 60 mA. */
#define PA_BIAS_CAL_TOL_FRAC 0.10f

/** Never iterate more than this; a bisection over 8 bits needs 8. */
#define PA_BIAS_CAL_MAX_ITERS 12

typedef struct {
    /** Command one gate-bias code.  Returns false if the DAC write failed. */
    bool (*write_code)(void *ctx, uint8_t code);
    /** Read the drain current in amps for the last commanded code. */
    float (*read_idq)(void *ctx);
    void *ctx;
} PaBiasCal_IO_t;

typedef struct {
    uint8_t code;        /**< the code the loop settled on */
    float   idq;         /**< the current measured at that code */
    int     iterations;
    bool    converged;   /**< within PA_BIAS_CAL_TOL_FRAC of the target */
    bool    at_floor;    /**< the target is out of reach: the rated-edge floor gives less current */
    bool    io_ok;       /**< every DAC write and ADC read succeeded */
} PaBiasCal_Result_t;

/**
 * Walk the gate bias to make the drain current equal target_a.
 *
 * Searches between PA_GATE_BIAS_CODE_MIN (the rated gate edge, most current) and
 * PA_GATE_BIAS_CODE_BOOT (the device's documented off state, no current), so the search range is
 * the device's own rated window and not a number chosen here.
 */
PaBiasCal_Result_t PaBiasCal_Run(const PaBiasCal_IO_t *io, float target_a);

/** One-line description for telemetry. */
const char *PaBiasCal_StatusName(const PaBiasCal_Result_t *r);

#endif /* PA_BIAS_CAL_H */
