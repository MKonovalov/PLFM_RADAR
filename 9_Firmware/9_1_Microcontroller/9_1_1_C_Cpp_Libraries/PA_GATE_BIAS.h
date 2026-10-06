/**
 * @file    PA_GATE_BIAS.h
 * @brief   Gate-bias transfer function for the 16 QPA2962 PAs -- single source of truth.
 *
 * Circuit (per channel, MainBoard):  DAC5578 VOUT -> Rin -> op-amp inverting input,
 * Rf from the op-amp output back to that input, + input on GND, V+ = +5V5_PA,
 * V- = -5V0_ADAR12/34.  VREFIN is the external 3V3_AN3_F rail (the DAC5578 has no
 * internal reference -- the pin-compatible DAC7678 is the one that does).  RSTSEL is
 * tied to that same rail, i.e. HIGH, which per the datasheet means "resets device to
 * mid-scale".
 *
 *   VGG = -(Rf/Rin) * (code / 255) * VREF     [8-bit mode, as the firmware configures it]
 *
 * Three datapoints, each grounded in a primary source:
 *
 *   code 128 (mid-scale) -> VGG = -4.03 V   the POR / CLR state.  This is verbatim
 *                                           Qorvo's shutdown condition for this part:
 *                                           "Reduce VG to -4.0 V. Ensure IDQ ~ 0 mA".
 *                                           A reset therefore parks the PAs OFF.
 *   code  38 (loop floor) -> VGG = -1.20 V  the most positive end of the device's
 *                                           rated gate range (QPA2962: VG typ -1.2 to
 *                                           -2.5 V at VD = 22 V, IDQ = 1680 mA).  The
 *                                           calibration loop walks the gate up to here
 *                                           and no further: it is the rated edge, not a
 *                                           guess.
 *   code 126 (boot bias) -> VGG = -3.98 V   the value the firmware's own bring-up comment
 *                                           states; it agrees with the transfer function
 *                                           above, which is an independent check that the
 *                                           gain and reference are what the design thinks.
 *
 * WHY THIS HEADER EXISTS: the one way to destroy this array is a gate that is not
 * negative enough.  0 V is *not* a rated bias for a depletion-mode GaN HEMT -- it is
 * fully enhanced with 22 V on the drain.  The emergency-stop path used to command
 * exactly that (it set the DAC clear code to zero-scale), so the clear code, the
 * clamp, and the floor now come from one place instead of being spelled out at each
 * call site.
 */
#ifndef PA_GATE_BIAS_H
#define PA_GATE_BIAS_H

/* the helpers are constexpr in C++ so the static_asserts below can use them */
#ifdef __cplusplus
#define PA_GB_INLINE constexpr
#else
#define PA_GB_INLINE static inline
#endif

/* --- schematic constants (MainBoard RADAR_Main_Board.sch, all 16 channels identical) --- */
#define PA_GATE_BIAS_RIN_OHM     1000.0f   /* DAC series resistor into the op-amp input */
#define PA_GATE_BIAS_RF_OHM      2443.0f   /* feedback resistor (2.443k, all 16 identical) */
#define PA_GATE_BIAS_VREF_V      3.3f      /* VREFIN = external 3V3_AN3_F */
#define PA_GATE_BIAS_GAIN        (PA_GATE_BIAS_RF_OHM / PA_GATE_BIAS_RIN_OHM)
#define PA_GATE_BIAS_RES_BITS    8
#define PA_GATE_BIAS_CODE_MAX    255

/* --- the three operating points --- */
#define PA_GATE_BIAS_CODE_OFF    128       /* mid-scale: the POR/CLR state */
#define PA_GATE_BIAS_VGG_OFF_V   (-4.03f)  /* = the datasheet's shutdown condition */
#define PA_GATE_BIAS_CODE_MIN    38        /* calibration-loop floor: the rated gate edge */
#define PA_GATE_BIAS_VGG_MAX_V   (-1.20f)  /* most positive *rated* gate voltage */
#define PA_GATE_BIAS_VGG_MIN_V   (-2.50f)  /* most negative rated gate voltage */
#define PA_GATE_BIAS_CODE_BOOT   126       /* bring-up bias */
#define PA_GATE_BIAS_VGG_BOOT_V  (-3.98f)

/** Gate voltage commanded by a DAC code (8-bit), per the transfer function above. */
PA_GB_INLINE float PA_GATE_BIAS_VggFromCode(int code)
{
    return -(PA_GATE_BIAS_GAIN * (float)code / (float)PA_GATE_BIAS_CODE_MAX) * PA_GATE_BIAS_VREF_V;
}

/** DAC code that commands a given gate voltage (rounded, not clamped). */
PA_GB_INLINE int PA_GATE_BIAS_CodeFromVgg(float vgg)
{
    /* single expression, so this is a valid C++11 constexpr */
    return (int)(((-vgg / PA_GATE_BIAS_GAIN) / PA_GATE_BIAS_VREF_V * (float)PA_GATE_BIAS_CODE_MAX)
                 + (((-vgg / PA_GATE_BIAS_GAIN) / PA_GATE_BIAS_VREF_V) >= 0.0f ? 0.5f : -0.5f));
}

/**
 * Clamp any commanded code into the range the device is rated for.
 * The upper bound is free (more negative = more off; the op-amp saturates at its V-
 * rail); the lower bound is the rated gate edge and must never be crossed, because
 * below it the drain current is set by nothing but the ADC feedback loop.
 */
PA_GB_INLINE int PA_GATE_BIAS_ClampCode(int code)
{
    return code < PA_GATE_BIAS_CODE_MIN ? PA_GATE_BIAS_CODE_MIN
                                        : (code > PA_GATE_BIAS_CODE_MAX ? PA_GATE_BIAS_CODE_MAX : code);
}

/* These must equal the driver's DAC5578_ClearCode_t; DA5578.c asserts that at
 * compile time so the two cannot drift apart. */
#define PA_GATE_BIAS_CLEAR_CODE_ZERO 0
#define PA_GATE_BIAS_CLEAR_CODE_MID  1
#define PA_GATE_BIAS_CLEAR_CODE_FULL 2
#define PA_GATE_BIAS_CLEAR_CODE_NOP  3

/**
 * Is a DAC5578 clear-code setting fail-safe for THIS array?
 * The clear code decides what the CLR pin -- which Emergency_Stop() asserts as its
 * first action, while the 22 V drain rail is still live -- drives the gates to.
 *   MID  (mid-scale) -> -4.03 V : off. Correct.
 *   FULL             -> more negative than off: also safe (the op-amp clamps).
 *   ZERO (zero-scale)-> 0 V : fully enhanced.  Never.
 *   NOP              -> retains whatever was last written: unknown.  Never.
 */
PA_GB_INLINE int PA_GATE_BIAS_ClearCodeIsFailSafe(int clear_code)
{
    return (clear_code == PA_GATE_BIAS_CLEAR_CODE_MID
            || clear_code == PA_GATE_BIAS_CLEAR_CODE_FULL);
}

#ifdef __cplusplus
constexpr float pa_gate_bias_absf(float x) { return x < 0.0f ? -x : x; }
constexpr bool pa_gate_bias_close(float a, float b, float tol) { return pa_gate_bias_absf(a - b) <= tol; }

/* The constants above are only meaningful if they agree with each other and with the
 * datasheet.  If someone changes Rf, Rin, VREF or the resolution, these fire. */
static_assert(pa_gate_bias_close(PA_GATE_BIAS_GAIN, 2.443f, 0.005f),
              "gate-bias gain must match Rf/Rin = 2443/1000 from the schematic");
static_assert(pa_gate_bias_close(PA_GATE_BIAS_VggFromCode(PA_GATE_BIAS_CODE_OFF), PA_GATE_BIAS_VGG_OFF_V, 0.05f),
              "mid-scale must land on the datasheet shutdown condition (-4.0 V)");
static_assert(pa_gate_bias_close(PA_GATE_BIAS_VggFromCode(PA_GATE_BIAS_CODE_MIN), PA_GATE_BIAS_VGG_MAX_V, 0.02f),
              "the calibration floor must be the most positive rated gate (-1.2 V)");
static_assert(pa_gate_bias_close(PA_GATE_BIAS_VggFromCode(PA_GATE_BIAS_CODE_BOOT), PA_GATE_BIAS_VGG_BOOT_V, 0.05f),
              "the bring-up bias must reproduce the value the firmware comment states (-3.98 V)");
static_assert(PA_GATE_BIAS_ClampCode(0) == PA_GATE_BIAS_CODE_MIN,
              "a zero code must clamp up to the rated gate edge, never pass through");
static_assert(!PA_GATE_BIAS_ClearCodeIsFailSafe(0),
              "zero-scale clear code means 0 V on the gates: fully enhanced. Not fail-safe.");
#endif /* __cplusplus */

#endif /* PA_GATE_BIAS_H */
