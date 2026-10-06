/**
 * @file    PA_BIAS_SEQUENCE.h
 * @brief   The order in which the PA gate bias and drain may be applied or removed.
 *
 * The array is 16 depletion-mode GaN HEMTs. Two facts from the QPA2962 datasheet drive this
 * file:
 *
 *   * the gate must be at or below the shutdown bias before drain voltage appears --
 *     "Reduce VG to -4.0 V. Ensure IDQ ~ 0 mA" is the documented off state; and
 *   * with the drain already up, the gate is then raised to the operating bias.
 *
 * So the two legal orders are:
 *
 *   ENABLE   gate -> OFF bias (-4.03 V, mid-scale), drain on, then gate -> operating bias
 *   DISABLE  gate -> OFF bias, drain off
 *
 * Applying drain voltage with the gate above pinch-off, or removing drain voltage while the
 * gate is at an operating bias, both stress the devices. The sequence lives here rather than
 * inline in main() so it can be tested -- the callbacks let a test observe the order of
 * operations without any hardware, and `PA_BiasSequence_Apply()` is the only thing that
 * touches the gate and the drain.
 *
 * Duty-cycling note: the point of this file for the thermal budget is that the bias is a
 * *state* that can be entered and left, not something left standing. See the plan's B2.1
 * arithmetic -- at the rated IDQ the array dissipates ~590 W, and the board's thermal path
 * supports roughly a fifth of that continuously.
 */
#ifndef PA_BIAS_SEQUENCE_H
#define PA_BIAS_SEQUENCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** The I/O the sequence needs, as callbacks, so the order can be tested without hardware. */
typedef struct {
    /** Write the same gate-bias code to all 16 channels (8-bit DAC code). */
    bool (*set_gate_code)(uint8_t code);
    /** Drive the PA drain enables (22 V RFPA VDD and the per-group +5V0_PA rails). */
    bool (*set_drain_enable)(bool on);
    /** Optional: millisecond delay between steps; may be NULL. */
    void (*delay_ms)(uint32_t ms);
} PA_BiasSequence_IO_t;

/** The bias values the sequence uses, so callers cannot invent their own. */
typedef struct {
    uint8_t gate_off_code;     /**< mid-scale: the documented shutdown state */
    uint8_t gate_operating_code;  /**< the calibrated Idq point */
    uint32_t settle_ms;        /**< delay after the drain comes up before raising the gate */
} PA_BiasSequence_Params_t;

/** How many steps a full apply takes; useful for tests and for sizing traces. */
#define PA_BIAS_SEQUENCE_ENABLE_STEPS  3
#define PA_BIAS_SEQUENCE_DISABLE_STEPS 2

/**
 * Apply or remove the PA bias in the documented order.
 *
 * enable = true:  gate -> gate_off_code, drain on, settle, gate -> gate_operating_code
 * enable = false: gate -> gate_off_code, drain off
 *
 * Returns false if any callback fails; on failure the drain is left off and the gate at the
 * off code, which is the safe state.
 */
bool PA_BiasSequence_Apply(const PA_BiasSequence_IO_t *io,
                           const PA_BiasSequence_Params_t *params,
                           bool enable);

/** Convenience: the off state only, for use before anything else is armed. */
bool PA_BiasSequence_ForceOff(const PA_BiasSequence_IO_t *io,
                              const PA_BiasSequence_Params_t *params);

#endif /* PA_BIAS_SEQUENCE_H */
