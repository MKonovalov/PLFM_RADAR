// PA_SENSE.h -- single source of truth for the RF PA current-sense chain
//
// Why this file exists (finding M4/M5 of the feasibility review):
//   the shunt value, the INA241 gain, the ADS7830 reference and the firmware
//   scaling constant were four independent copies of the same physical fact.
//   They had drifted apart: the PA BOM value column said 5 mR while the
//   specified part (WSL2816R1000FEH) is 0.1 ohm, and the firmware scaled with
//   3.3 V while the ADS7830 is initialised with its internal 2.5 V reference.
//   Every one of those mismatches silently mis-biases the PAs.
//
// Change a value here and the whole chain (scaling, thresholds, compile-time
// sanity checks, tests) follows. Do not inline these numbers anywhere else.

#ifndef PA_SENSE_H
#define PA_SENSE_H

#include <stdint.h>

// ---- Hardware values (must match the schematic / BOM / assembly) -----------
#ifndef PA_SHUNT_OHMS
#define PA_SHUNT_OHMS 0.005f       // R10 on the PA board (5 mOhm)
#endif
#ifndef PA_INA_GAIN
#define PA_INA_GAIN 100.0f         // INA241A4 = 100 V/V (Rev B; A3 was 50 V/V)
#endif
#ifndef ADS7830_VREF_VOLTS
#define ADS7830_VREF_VOLTS 2.5f    // ADS7830 internal reference (PDIRON_ADON)
#endif
#define ADS7830_CODE_MAX 255.0f

// ---- Targets --------------------------------------------------------------
/*
 * ---- The PA choice (issue #21): the constants the migration needs -------------------------------
 *
 * This chain is deliberately written as constants so the device change is an edit here and nowhere
 * else.  For the QPA1010 (24 V, IDQ = 600 mA, per its datasheet):
 *
 *     PA_IDQ_TARGET_A  0.6f      was 1.680f for the QPA2962
 *     PA_IDQ_OC_TRIP_A 0.9f      was 2.5f
 *
 * The trip is the one that matters.  It is NOT enough to move the target: at the new 600 mA bias
 * point a 2.5 A trip sits at 4.2x the operating current, so the part could run at four times its
 * rated bias before anything fired - the protection would be effectively absent.  Scaling the trip
 * to keep the present ratio (2.5 / 1.680 = 1.49x) gives 0.9 A.
 *
 * Both values keep every static_assert below true, so the migration is a constants edit and not a
 * redesign of the sense chain:
 *
 *     full scale 5.0 A  >  0.9 x 1.5 = 1.35 A          (with margin)
 *     target      0.6 A <  full scale 5.0 A
 *     trip        0.9 A <  0.6 x 1.7 = 1.02 A
 *
 * Resolution at the new point: 19.6 mA per LSB, so 600 mA reads about code 31 and 0.9 A about code
 * 46 - coarse but ample for a trip.  The un-biased threshold (PA_IDQ_BIAS_FAULT_A, 0.1 A) stays
 * usable: it becomes 17 % of the operating point rather than 6 %, which is tighter but still below
 * a healthy bias.
 *
 * NOT APPLIED: the PA part has not been chosen.  These are recorded so that when it is, the change
 * is a two-line edit at the call site with the arithmetic already checked.
 */

#ifndef PA_IDQ_TARGET_A
#define PA_IDQ_TARGET_A 1.680f     // QPA2962 datasheet bias point
#endif
#ifndef PA_IDQ_OC_TRIP_A
#define PA_IDQ_OC_TRIP_A 2.5f      // over-current threshold used by the health check
#endif
#ifndef PA_IDQ_BIAS_FAULT_A
#define PA_IDQ_BIAS_FAULT_A 0.1f   // below this the channel is considered un-biased
#endif

// ---- Derived quantities ---------------------------------------------------
constexpr float PA_SENSE_VOLTS_PER_AMP = PA_SHUNT_OHMS * PA_INA_GAIN;
constexpr float PA_SENSE_FULL_SCALE_A  = ADS7830_VREF_VOLTS / PA_SENSE_VOLTS_PER_AMP;
constexpr float PA_SENSE_AMPS_PER_LSB  = PA_SENSE_FULL_SCALE_A / ADS7830_CODE_MAX;

// Compile-time sanity: the protection threshold has to be reachable. This is the
// check that catches a wrong shunt / wrong amplifier gain / wrong reference
// combination (with a 0.1 ohm shunt the ADC saturates at ~0.5 A and the
// threshold can never be reached -- the failure the BOM currently sets up).
static_assert(PA_SENSE_FULL_SCALE_A > PA_IDQ_OC_TRIP_A * 1.5f,
              "PA sense chain saturates before the over-current threshold: "
              "check PA_SHUNT_OHMS / PA_INA_GAIN / ADS7830_VREF_VOLTS");
static_assert(PA_IDQ_TARGET_A < PA_SENSE_FULL_SCALE_A,
              "bias target above the measurable range");
static_assert(PA_IDQ_OC_TRIP_A < PA_IDQ_TARGET_A * 1.7f,
              "over-current threshold leaves no headroom over the bias point");

// ---- Conversion -----------------------------------------------------------
static inline float paSenseCodeToAmps(uint8_t code)
{
    return (ADS7830_VREF_VOLTS / ADS7830_CODE_MAX) * (float)code / PA_SENSE_VOLTS_PER_AMP;
}

static inline uint8_t paSenseAmpsToCode(float amps)
{
    float code = (amps * PA_SENSE_VOLTS_PER_AMP) / ADS7830_VREF_VOLTS * ADS7830_CODE_MAX;
    if (code < 0.0f) code = 0.0f;
    if (code > ADS7830_CODE_MAX) code = ADS7830_CODE_MAX;
    return (uint8_t)(code + 0.5f);
}

#endif // PA_SENSE_H
