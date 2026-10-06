/**
 * @file    PA_BIAS_SEQUENCE.c
 * @brief   The order in which the PA gate bias and drain may be applied or removed.
 *          See PA_BIAS_SEQUENCE.h for why the order is not negotiable.
 */
#include "PA_BIAS_SEQUENCE.h"

bool PA_BiasSequence_ForceOff(const PA_BiasSequence_IO_t *io,
                              const PA_BiasSequence_Params_t *params)
{
    if (io == NULL || params == NULL || io->set_gate_code == NULL) {
        return false;
    }
    /* gate to the shutdown bias first; the drain is not touched */
    if (!io->set_gate_code(params->gate_off_code)) {
        return false;
    }
    if (io->set_drain_enable != NULL && !io->set_drain_enable(false)) {
        return false;
    }
    return true;
}

bool PA_BiasSequence_Apply(const PA_BiasSequence_IO_t *io,
                           const PA_BiasSequence_Params_t *params,
                           bool enable)
{
    if (io == NULL || params == NULL || io->set_gate_code == NULL ||
        io->set_drain_enable == NULL) {
        return false;
    }

    if (!enable) {
        /* DISABLE: gate off first, then remove drain voltage. */
        if (!io->set_gate_code(params->gate_off_code)) {
            return false;
        }
        return io->set_drain_enable(false);
    }

    /* ENABLE: gate off, drain up, settle, then raise the gate to the operating point. */
    if (!io->set_gate_code(params->gate_off_code)) {
        return false;                    /* leave the drain off; the gate is already safe */
    }
    if (!io->set_drain_enable(true)) {
        /* the drain did not come up -- put the gate back to the off state and report */
        (void)io->set_gate_code(params->gate_off_code);
        return false;
    }
    if (io->delay_ms != NULL && params->settle_ms > 0) {
        io->delay_ms(params->settle_ms);
    }
    if (!io->set_gate_code(params->gate_operating_code)) {
        /* could not reach the operating bias: return to the safe state, drain off */
        (void)io->set_gate_code(params->gate_off_code);
        (void)io->set_drain_enable(false);
        return false;
    }
    return true;
}
