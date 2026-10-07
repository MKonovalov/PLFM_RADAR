/**
 * @file    PA_THERMAL.h
 * @brief   The PA array's thermal budget, in code rather than in prose (issue #5).
 *
 * The QPA2962 brief carries the numbers that were previously assumed unavailable:
 *
 *   Thermal Resistance (theta_JC)  2.83 degC/W   T_BASE = 85 degC, VD = 22 V, IDQ = 1680 mA,
 *                                                 no RF, P_DISS = 36.96 W
 *   Channel Temperature, T_CH      189 degC       under RF, IR-scan equivalent
 *
 * Those two rows are self-consistent - 85 + 2.83 x 36.96 = 189.6 - which is what makes them
 * usable: the channel sits theta_JC above the case, so a measured case temperature converts to
 * a channel temperature with one multiplication.  The firmware's existing 75 degC limit on the
 * PA sensors therefore corresponds to a channel of about 180 degC, comfortably inside the GaN
 * limit; what was missing was never the limit but the sensor (see the eight Temperature_N
 * variables, which no board populates).
 *
 * The reference for the channel-temperature basis is Qorvo's own note, "GaN Device TCHMAX
 * Theta-JC and Reliability Estimates" (da006480), which the brief points at.
 */
#ifndef PA_THERMAL_H
#define PA_THERMAL_H

#include <stdbool.h>

/** From the QPA2962 brief: junction-to-case, measured to the back of the package. */
#define PA_THETA_JC_C_PER_W 2.83f

/** Quiescent dissipation at VD = 22 V, IDQ = 1680 mA with no RF (the brief's test condition). */
#define PA_PDISS_QUIESCENT_W 36.96f

/**
 * Dissipation under drive: P_DC minus the RF that leaves the part.  At the brief's 10 W
 * saturated output and 22 % PAE, P_DC = 10 / 0.22 = 45.5 W, so about 35.5 W is dissipated -
 * slightly less than quiescent, which is why the quiescent figure is the safe one to plan with.
 */
#define PA_PDISS_DRIVEN_W 35.5f

/** Design limit on channel temperature.  Conservative against the brief's 189 degC under RF. */
#define PA_TCH_MAX_C 200.0f

/** The case temperature that corresponds to the channel limit at quiescent dissipation. */
#define PA_TCASE_MAX_C (PA_TCH_MAX_C - PA_THETA_JC_C_PER_W * PA_PDISS_QUIESCENT_W)

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
