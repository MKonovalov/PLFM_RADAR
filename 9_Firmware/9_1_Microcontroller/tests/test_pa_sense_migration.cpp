/*
 * #21: the applied QPA1010 sense configuration, checked against its datasheet.
 *
 * When the migration was only *planned*, this file defined the QPA1010's constants before including
 * the header and proved that combination was usable.  The migration is now applied, so those values
 * are the header's own defaults - and what is worth checking has changed: that the defaults really
 * are the datasheet's numbers, and that the protection they give is sound.
 *
 * The QPA2962's values remain reachable by defining them before the header is read:
 *
 *     #define PA_IDQ_TARGET_A  1.680f
 *     #define PA_IDQ_OC_TRIP_A 2.5f
 *     #include "PA_SENSE.h"
 *
 * which is what makes the device choice a build-level override rather than an edit.
 */
#include <stdio.h>
#include <stdbool.h>

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
    printf("=== the applied QPA1010 sense configuration ===\n");

    /* the datasheet's numbers, not the previous device's */
    check(PA_IDQ_TARGET_A == 0.600f, "the target is the QPA1010's 600 mA, not the QPA2962's 1680 mA");
    check(PA_IDQ_OC_TRIP_A == 0.9f, "the trip moved with it, to 0.9 A");

    printf("       sense: %.3f V/A, %.2f A full scale, %.1f mA per LSB\n",
           (double)PA_SENSE_VOLTS_PER_AMP, (double)PA_SENSE_FULL_SCALE_A,
           (double)(PA_SENSE_AMPS_PER_LSB * 1000.0f));

    /* the protection has to sit near the operating point, not merely above it */
    const float ratio = PA_IDQ_OC_TRIP_A / PA_IDQ_TARGET_A;
    check(ratio > 1.2f && ratio < 1.7f,
          "the trip sits 1.2-1.7x the target, as the header's own static_assert requires");
    printf("       trip/target = %.2fx (the QPA2962 build was 2.5/1.680 = 1.49x)\n", (double)ratio);

    /* the mistake this guards against: moving the target and leaving the trip */
    const float stale_trip_ratio = 2.5f / PA_IDQ_TARGET_A;
    check(stale_trip_ratio > 4.0f,
          "keeping the 2.5 A trip would sit above 4x the new bias - the reason it had to move");
    printf("       2.5 A against a 600 mA device would be %.1fx\n", (double)stale_trip_ratio);

    /* resolution and range */
    check(PA_SENSE_AMPS_PER_LSB < 0.025f, "the LSB stays under 25 mA");
    check(PA_IDQ_OC_TRIP_A / PA_SENSE_AMPS_PER_LSB < (float)ADS7830_CODE_MAX,
          "the trip is inside the ADC's code range");
    printf("       the trip is code %.0f, the target code %.0f\n",
           (double)(PA_IDQ_OC_TRIP_A / PA_SENSE_AMPS_PER_LSB),
           (double)(PA_IDQ_TARGET_A / PA_SENSE_AMPS_PER_LSB));

    /* a dead channel is still distinguishable from a healthy one */
    check(PA_IDQ_BIAS_FAULT_A < PA_IDQ_TARGET_A * 0.5f,
          "the un-biased threshold is still comfortably below a healthy bias");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
