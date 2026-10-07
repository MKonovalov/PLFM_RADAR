/**
 * @file    PA_BIAS_CAL.c
 * @brief   Bisection on the gate-bias code (issue #21).  See the header for why.
 */
#include "PA_BIAS_CAL.h"

#include "PA_GATE_BIAS.h"

const char *PaBiasCal_StatusName(const PaBiasCal_Result_t *r)
{
    if (!r || !r->io_ok)      return "IO-FAULT";
    if (r->at_floor)          return "AT-FLOOR";
    if (r->converged)         return "CONVERGED";
    return "NO-SOLUTION";
}

PaBiasCal_Result_t PaBiasCal_Run(const PaBiasCal_IO_t *io, float target_a)
{
    PaBiasCal_Result_t res = { 0, 0.0f, 0, false, false, false };
    if (!io || !io->write_code || !io->read_idq || target_a <= 0.0f) {
        return res;
    }

    /* The range is the device's own rated window: the floor is the most positive rated gate and
     * therefore the most current; the boot code is the documented off state and therefore none.
     * A lower code is a LESS negative gate, so current rises as the code falls - which is the
     * ordering the bisection depends on. */
    uint8_t lo = (uint8_t)PA_GATE_BIAS_CODE_MIN;     /* most current */
    uint8_t hi = (uint8_t)PA_GATE_BIAS_CODE_BOOT;    /* no current  */
    if (hi <= lo) {
        return res;                                   /* nonsensical constants: do nothing */
    }

    const float tol = PA_BIAS_CAL_TOL_FRAC * target_a;
    uint8_t best_code = hi;
    float   best_idq  = 0.0f;
    bool    have_best = false;

    res.io_ok = true;

    while (hi > lo && res.iterations < PA_BIAS_CAL_MAX_ITERS) {
        const uint8_t code = (uint8_t)((lo + hi) / 2u);
        res.iterations++;

        if (!io->write_code(io->ctx, PA_GATE_BIAS_ClampCode(code))) {
            res.io_ok = false;
            break;
        }
        const float idq = io->read_idq(io->ctx);
        res.code = code;
        res.idq  = idq;

        /* Keep the closest reading seen, so a final state can be reported even without convergence. */
        if (!have_best || (idq > target_a ? idq - target_a : target_a - idq) <
                          (best_idq > target_a ? best_idq - target_a : target_a - best_idq)) {
            best_code = code;
            best_idq  = idq;
            have_best = true;
        }

        if (idq >= target_a - tol && idq <= target_a + tol) {
            res.converged = true;
            return res;
        }

        if (idq < target_a) {
            /* not enough current: move toward the floor, which is the higher-current end */
            hi = (uint8_t)(code - 1u);
        } else {
            lo = (uint8_t)(code + 1u);
        }
    }

    if (!res.io_ok) {
        return res;
    }

    /* Out of range: the floor itself does not reach the target.  Say so rather than report the
     * nearest code as if it were the answer. */
    if (!io->write_code(io->ctx, PA_GATE_BIAS_ClampCode((uint8_t)PA_GATE_BIAS_CODE_MIN))) {
        res.io_ok = false;
        return res;
    }
    const float floor_idq = io->read_idq(io->ctx);
    if (floor_idq < target_a - tol) {
        res.code = (uint8_t)PA_GATE_BIAS_CODE_MIN;
        res.idq  = floor_idq;
        res.at_floor = true;
        return res;
    }

    /* Otherwise report the closest point found. */
    res.code = best_code;
    res.idq  = best_idq;
    return res;
}
