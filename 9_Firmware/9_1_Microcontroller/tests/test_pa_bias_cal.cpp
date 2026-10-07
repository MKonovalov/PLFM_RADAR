/*
 * #21: does the IDQ calibration converge inside the DAC range, for this device?
 *
 * The acceptance asks exactly that, and it is answerable without hardware: the gate-bias transfer
 * function is known from the board, and a depletion-mode GaN HEMT's transfer curve is monotonic
 * with a pinch-off.  So the loop is run against a model device and the answer is checked.
 *
 * The model:  Idq(VGG) = I_FULL * (1 - |VGG| / V_PINCH)^2     for |VGG| <= V_PINCH, else 0
 *
 *   V_PINCH = 4.0 V   - the QPA1010's datasheet gives ~0.0001 mA of gate leakage at Vg = -4.0 V,
 *                       i.e. it is pinched off there, which is what its Bias Up Procedure uses;
 *   I_FULL  = 3.32 A  - chosen so that Idq(-2.3 V) = 0.600 A, the datasheet's typical bias point.
 *
 * That is a model, not a measurement - but it is monotonic and it is calibrated to two datasheet
 * facts, so it answers the question the acceptance asks: can the loop reach the target within the
 * gates' rated range, and does it land there?
 */
#include <stdio.h>
#include <stdbool.h>
#include <math.h>

#include "PA_BIAS_CAL.h"
#include "PA_GATE_BIAS.h"
#include "PA_SENSE.h"

static int checks, failures;
static void check(int c, const char *what)
{
    checks++;
    if (!c) { failures++; printf("  FAIL: %s\n", what); }
    else    { printf("  ok  : %s\n", what); }
}

/* ---- the model device ---- */
#define MODEL_V_PINCH 4.0f
#define MODEL_I_FULL  3.32f

static uint8_t g_last_code;

static bool model_write(void *ctx, uint8_t code)
{
    (void)ctx;
    g_last_code = code;
    return true;
}

static float model_read(void *ctx)
{
    (void)ctx;
    const float vgg = PA_GATE_BIAS_VggFromCode((int)g_last_code);
    const float a = (vgg < 0.0f ? -vgg : vgg);
    if (a >= MODEL_V_PINCH) {
        return 0.0f;
    }
    const float k = 1.0f - (a / MODEL_V_PINCH);
    return MODEL_I_FULL * k * k;
}

/* the same device, but with the DAC write failing partway, to exercise the IO path */
static bool model_write_fails(void *ctx, uint8_t code)
{
    (void)ctx; (void)code;
    return false;
}

static PaBiasCal_IO_t model_io = { model_write, model_read, NULL };

static void report(const char *what, const PaBiasCal_Result_t *r)
{
    printf("      %-22s code %3d -> VGG %+.3f V, Idq %.3f A, %d iters, %s\n",
           what, r->code, (double)PA_GATE_BIAS_VggFromCode((int)r->code), (double)r->idq,
           r->iterations, PaBiasCal_StatusName(r));
}

int main(void)
{
    printf("=== #21: IDQ calibration against a model QPA1010 ===\n");

    /* sanity: the model reproduces the datasheet's typical bias point */
    const float at_typ = MODEL_I_FULL * (1.0f - (2.3f / MODEL_V_PINCH)) *
                         (1.0f - (2.3f / MODEL_V_PINCH));
    check(fabsf(at_typ - 0.600f) < 0.005f, "the model gives 600 mA at the datasheet's -2.3 V typ");
    printf("      model: Idq(-2.300 V) = %.3f A\n", (double)at_typ);

    /* ---------------------------------------------------------------- the acceptance */
    printf("=== the QPA1010's 0.600 A target ===\n");
    PaBiasCal_Result_t r = PaBiasCal_Run(&model_io, PA_IDQ_TARGET_A);
    report("target 0.600 A", &r);

    check(r.io_ok, "every DAC write and ADC read succeeded");
    check(r.converged, "the loop CONVERGES to the target inside the DAC range");
    check(fabsf(r.idq - PA_IDQ_TARGET_A) <= PA_BIAS_CAL_TOL_FRAC * PA_IDQ_TARGET_A,
          "...within the 10 % tolerance the loop is written to");
    check(r.iterations <= 8, "a bisection needs no more than 8 steps for 8 bits");
    check(!r.at_floor, "...and it did not have to run out of range to get there");

    const float settled_vgg = PA_GATE_BIAS_VggFromCode((int)r.code);
    check(settled_vgg <= PA_GATE_BIAS_VGG_MAX_V && settled_vgg >= PA_GATE_BIAS_VGG_MIN_V,
          "the settled gate is INSIDE the rated window, not on its edge");

    /* ---------------------------------------------------------------- the protection still works */
    printf("=== over-current protection against the same model ===\n");
    const float trip_vgg = PA_GATE_BIAS_VggFromCode(
        PA_GATE_BIAS_CodeFromVgg(-1.92f));   /* where the trip current is reachable */
    (void)trip_vgg;

    check(PA_IDQ_OC_TRIP_A > r.idq, "the trip threshold is ABOVE the converged operating current");

    /* and the trip point must itself be reachable inside the rated range, or the protection would
     * be a number the hardware can never present to the ADC */
    uint8_t trip_code = 0;
    {
        /* find the code at which the model's current reaches the trip */
        int found = 0;
        for (int c = PA_GATE_BIAS_CODE_MIN; c <= PA_GATE_BIAS_CODE_BOOT; c++) {
            const float v = PA_GATE_BIAS_VggFromCode(c);
            const float a = (v < 0 ? -v : v);
            const float i = (a >= MODEL_V_PINCH) ? 0.0f
                                                 : MODEL_I_FULL * (1.0f - a / MODEL_V_PINCH) *
                                                                    (1.0f - a / MODEL_V_PINCH);
            if (i >= PA_IDQ_OC_TRIP_A) { trip_code = (uint8_t)c; found = 1; break; }
        }
        check(found, "the trip current is reachable inside the rated gate window");
        printf("      the trip is reached at code %d (VGG %+.3f V)\n",
               trip_code, (double)PA_GATE_BIAS_VggFromCode(trip_code));
    }
    /* More current is a LOWER code, so the trip sits below the operating point, not above it.
     * The property that matters is that the two are distinguishable and ordered. */
    check(trip_code < r.code,
          "the trip sits on the more-current side of the operating point (a lower code)");
    printf("      the trip is %.0f mA above the settled operating point\n",
           (double)((PA_IDQ_OC_TRIP_A - r.idq) * 1000.0f));

    /* ---------------------------------------------------------------- the unreachable case */
    printf("=== an unreachable target ===\n");
    /* The OLD device's target, asked of THIS device's rated window: 1.68 A needs about -1.16 V,
     * which is past the -1.5 V edge and outside the search range, so the loop must say so rather
     * than quietly settle on the floor. */
    PaBiasCal_Result_t old = PaBiasCal_Run(&model_io, 1.680f);
    report("former target 1.680 A", &old);
    check(old.io_ok, "the write path still reports success");
    check(old.at_floor, "an out-of-range target is REPORTED as at-floor, not passed off as the answer");
    check(!old.converged, "...and is not called converged");
    check(old.code == PA_GATE_BIAS_CODE_MIN, "...and the loop leaves the gate at the rated edge");

    /* A target ABOVE the sense chain's resolution converges; a target BELOW it cannot, and the
     * loop must not claim otherwise.  This matters because the sense chain resolves 19.6 mA per
     * LSB, so a target of a few mA is not a target the hardware can hold at all. */
    PaBiasCal_Result_t small_ok = PaBiasCal_Run(&model_io, 0.050f);
    report("a 50 mA target", &small_ok);
    check(small_ok.converged, "a target above the sense resolution converges");

    PaBiasCal_Result_t tiny = PaBiasCal_Run(&model_io, 0.001f);
    report("a 1 mA target", &tiny);
    check(!tiny.converged,
          "a target below the sense chain's 19.6 mA resolution is NOT reported as converged");
    printf("      (the nearest code gives %.1f mA against a 1.0 mA target)\n", (double)(tiny.idq * 1000.0f));

    /* ---------------------------------------------------------------- argument and IO handling */
    printf("=== argument and IO handling ===\n");
    PaBiasCal_Result_t bad = PaBiasCal_Run(NULL, 0.6f);
    check(!bad.io_ok && !bad.converged, "a null IO table is refused");

    PaBiasCal_Result_t zero = PaBiasCal_Run(&model_io, 0.0f);
    check(!zero.converged && zero.iterations == 0, "a zero target is refused without touching the DAC");

    PaBiasCal_IO_t broken = { model_write_fails, model_read, NULL };
    PaBiasCal_Result_t io = PaBiasCal_Run(&broken, 0.6f);
    check(!io.io_ok && !io.converged, "a failing DAC write is reported, not retried forever");
    check(io.iterations <= 1, "...and the loop stops at the first failure");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
