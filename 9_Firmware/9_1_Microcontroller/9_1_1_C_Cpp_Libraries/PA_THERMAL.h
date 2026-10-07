/**
 * @file    PA_THERMAL.h
 * @brief   The PA array's thermal budget, in code rather than in prose (issue #5).
 *
 * The chosen device is the QPA1010 (issue #21).  Its datasheet gives:
 *
 *   Thermal Resistance (theta_JC)  2.60 degC/W
 *   Recommended operating          VD 24 V, IDQ 600 mA, T_BASE -40 to +85 degC
 *   Absolute maximum               P_DISS 38 W at 85 degC, CW
 *   Test conditions                pulsed VD, PW = 100 us, DC = 10 %
 *   Output power / PAE             ~15 W (41.8 dBm) at ~37.7 %
 *
 * so the channel sits theta_JC above the case and a measured case temperature converts to a
 * channel temperature with one multiplication, exactly as before.  The firmware's 75 degC limit on
 * the PA sensors stays well inside the datasheet's 85 degC T_BASE; what was ever missing was not
 * the limit but the sensor (see the eight Temperature_N variables, which no board populates).
 *
 * The reference for the channel-temperature basis is Qorvo's own note, "GaN Device TCHMAX
 * Theta-JC and Reliability Estimates" (da006480).
 */
#ifndef PA_THERMAL_H
#define PA_THERMAL_H

#include <stdbool.h>

/** QPA1010 datasheet: junction-to-case, measured to the back of the package. */
#define PA_THETA_JC_C_PER_W 2.60f

/** Quiescent dissipation at VD = 24 V, IDQ = 600 mA with no RF: 24 x 0.6. */
#define PA_PDISS_QUIESCENT_W 14.4f

/**
 * Dissipation under drive: P_DC minus the RF that leaves the part.  At ~15 W output and ~37.7 % PAE,
 * P_DC = 15 / 0.377 = 39.8 W, so 24.8 W is dissipated.
 *
 * NOTE the direction change from the QPA2962: there the quiescent figure (36.96 W) was the larger
 * and therefore the conservative one to plan with.  Here driven (24.8 W) is nearly twice quiescent
 * (14.4 W), so DRIVEN is the safe figure.  A planner who carried the old habit across would be
 * planning against half the real load.
 */
#define PA_PDISS_DRIVEN_W 24.8f

/** The larger of the two - what the thermal plan and the case limit must be built on. */
#define PA_PDISS_PLAN_W ((PA_PDISS_DRIVEN_W > PA_PDISS_QUIESCENT_W) ? PA_PDISS_DRIVEN_W : PA_PDISS_QUIESCENT_W)

/** Design limit on channel temperature, on the same GaN basis as before. */
#define PA_TCH_MAX_C 200.0f

/** The case temperature that corresponds to the channel limit at the planning dissipation. */
#define PA_TCASE_MAX_C (PA_TCH_MAX_C - PA_THETA_JC_C_PER_W * PA_PDISS_PLAN_W)

/** Design limit on the case itself: the datasheet's T_BASE maximum. */
#define PA_TCASE_ABS_MAX_C 85.0f

/** Channel temperature for a measured case temperature and dissipation. */
float PA_Thermal_ChannelTemp(float case_c, float pdiss_w);

/** True while the channel stays inside the design limit. */
bool PA_Thermal_IsSafe(float case_c, float pdiss_w);

#endif /* PA_THERMAL_H */

/**
 * The NTC in the eight temperature probes (issue #5).
 *
 * The main board already carries the interface: eight 3-pin headers (JP5, JP6, JP11, JP12, JP14,
 * JP15, JP16, JP19) each bringing out +3V3_AN4_F, an ADS7830 channel and GND, plus the third
 * ADS7830 (U89, address 0x4B) that the firmware reads as hadc3.  What the board lacked was the
 * pull-up, and what the firmware lacked was the conversion - the bring-up read stored raw 8-bit
 * codes while the loop read applied a linear scale, and neither is right for a thermistor.
 *
 * The probe is a 10k NTC with B(25/85) = 3434 K on a 10k pull-up to +3V3_AN4_F, which is also the
 * ADC's reference - so the code is the divider ratio and the absolute rail voltage cancels.
 */
#define PA_NTC_R25_OHM    10000.0f    /**< probe resistance at 25 degC */
#define PA_NTC_T25_K      298.15f     /**< 25 degC in kelvin */
#define PA_NTC_B_K        3434.0f     /**< B(25/85) from the probe datasheet */
#define PA_NTC_PULLUP_OHM 10000.0f    /**< the on-board pull-up to +3V3_AN4_F */
/** At or above this ratio the input sits on the rail: an unplugged probe, not a cold one. */
#define PA_NTC_MAX_RATIO  0.995f

/**
 * Convert a divider ratio (0..1) to a probe temperature in degC.
 * Returns false when the reading cannot be a real temperature - most importantly an open probe,
 * which would otherwise be indistinguishable from a very cold one.
 */
bool PA_Thermal_NtcToCelsius(float ratio, float *celsius_out);
