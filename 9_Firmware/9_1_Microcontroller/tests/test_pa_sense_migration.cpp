/*
 * #21: prove the sense chain survives the PA migration as a constants edit.
 *
 * The issue's acceptance asks that the calibration converge to the new target inside the DAC range
 * and that "over-current protection still trips above it".  The first is a bench measurement; the
 * second is arithmetic, and this is where it is checked - by compiling the same header the firmware
 * uses with the QPA1010's constants in place of the QPA2962's.
 *
 * The header's own static_asserts fire if the combination is unusable, so arriving here at all is
 * part of the test.  What this adds is the reasoning made explicit and a check that the trip
 * actually sits where the protection needs it, rather than merely somewhere above the target.
 */
#include <stdio.h>
#include <stdbool.h>

/* the migration, before the header is read */
#define PA_IDQ_TARGET_A   0.6f     /* QPA1010 datasheet bias point, 24 V */
#define PA_IDQ_OC_TRIP_A  0.9f     /* scaled to keep the QPA2962's trip/target ratio */

#include "PA_SENSE.h"

static int checks, failures;
static void check(int c, const char *what)
{
    checks++;
    if (!c) { failures++; printf("  FAIL: %s\n", what); }
    else    { printf("  ok  : %s\n", what); }
}

int main(void)
{
    printf("=== the sense chain with the QPA1010's constants ===\n");

    check(PA_IDQ_TARGET_A == 0.6f, "the target is the QPA1010's 600 mA");
    check(PA_SENSE_FULL_SCALE_A > 5.0f - 0.01f && PA_SENSE_FULL_SCALE_A < 5.0f + 0.01f,
          "full scale is 5.0 A: 0.5 V/A into a 2.5 V reference");

    /* the protection has to be near the operating point, not merely above it */
    const float ratio = PA_IDQ_OC_TRIP_A / PA_IDQ_TARGET_A;
    check(ratio > 1.2f && ratio < 1.7f,
          "the trip sits 1.2-1.7x the target, as the header's own static_assert requires");
    printf("       trip/target = %.2fx (the QPA2962 build is 2.5/1.680 = 1.49x)\n", (double)ratio);

    /* and the old trip would NOT have been acceptable - this is the finding */
    const float old_trip_ratio = 2.5f / PA_IDQ_TARGET_A;
    check(old_trip_ratio > 4.0f,
          "keeping the 2.5 A trip would put it above 4x the new bias point - the reason the trip moves");
    printf("       2.5 A against a 600 mA device = %.1fx\n", (double)old_trip_ratio);

    /* resolution: coarse, but a trip does not need fine resolution */
    check(PA_SENSE_AMPS_PER_LSB < 0.025f, "the LSB stays under 25 mA");
    check(PA_IDQ_OC_TRIP_A / PA_SENSE_AMPS_PER_LSB < (float)ADS7830_CODE_MAX,
          "the trip is inside the ADC's code range");
    printf("       %.1f mA per LSB; the trip is code %.0f, the target code %.0f\n",
           (double)(PA_SENSE_AMPS_PER_LSB * 1000.0f),
           (double)(PA_IDQ_OC_TRIP_A / PA_SENSE_AMPS_PER_LSB),
           (double)(PA_IDQ_TARGET_A / PA_SENSE_AMPS_PER_LSB));

    /* the un-biased detector still distinguishes a dead channel from a healthy one */
    check(PA_IDQ_BIAS_FAULT_A < PA_IDQ_TARGET_A * 0.5f,
          "the un-biased threshold is still comfortably below a healthy bias");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
