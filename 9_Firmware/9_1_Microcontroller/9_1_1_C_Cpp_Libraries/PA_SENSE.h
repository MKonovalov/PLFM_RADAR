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
#define PA_INA_GAIN 50.0f          // INA241A3 = 50 V/V (A4 = 100 V/V, Rev B part)
#endif
#ifndef ADS7830_VREF_VOLTS
#define ADS7830_VREF_VOLTS 2.5f    // ADS7830 internal reference (PDIRON_ADON)
#endif
#define ADS7830_CODE_MAX 255.0f

// ---- Targets --------------------------------------------------------------
#define PA_IDQ_TARGET_A 1.680f     // QPA2962 datasheet bias point
#define PA_IDQ_OC_TRIP_A 2.5f      // over-current threshold used by the health check
#define PA_IDQ_BIAS_FAULT_A 0.1f   // below this the channel is considered un-biased

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
