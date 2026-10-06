/* Gate-bias safety: the transfer function, the clamp, and the clear-code contract.
 *
 * Regression target: the emergency stop used to set the DAC clear code to zero-scale,
 * which parks all 16 GaN gates at 0 V -- fully enhanced, 22 V on the drains -- for the
 * duration of the shutdown sequence.  These are the checks that make that state
 * unreachable, plus the arithmetic behind the three operating points.
 */
#include "PA_GATE_BIAS.h"
#include <cstdio>
#include <cmath>

static int failures = 0;
static int checks = 0;

static void check(bool ok, const char *what)
{
    checks++;
    if (!ok) { failures++; printf("   FAIL: %s\n", what); }
}
static void close_to(float got, float want, float tol, const char *what)
{
    checks++;
    if (fabsf(got - want) > tol) {
        failures++;
        printf("   FAIL: %s (got %.4f, want %.4f +/- %.4f)\n", what, got, want, tol);
    }
}

int main(void)
{
    printf("=== gate-bias transfer function (8-bit, VREF=3.3V, gain=Rf/Rin=2.443) ===\n");
    close_to(PA_GATE_BIAS_VggFromCode(128), -4.03f, 0.05f, "mid-scale = -4.03 V (POR/CLR state)");
    close_to(PA_GATE_BIAS_VggFromCode(38),  -1.20f, 0.02f, "floor = -1.20 V (rated gate edge)");
    close_to(PA_GATE_BIAS_VggFromCode(126), -3.98f, 0.05f, "boot bias = -3.98 V (firmware comment)");
    close_to(PA_GATE_BIAS_VggFromCode(0),    0.00f, 0.01f, "code 0 = 0 V = fully enhanced (the bad state)");

    printf("=== monotonic: more code = more negative gate = less current ===\n");
    bool mono = true;
    for (int c = PA_GATE_BIAS_CODE_MIN; c < PA_GATE_BIAS_CODE_MAX; c++)
        if (!(PA_GATE_BIAS_VggFromCode(c + 1) < PA_GATE_BIAS_VggFromCode(c))) mono = false;
    check(mono, "Vgg decreases as the code rises");

    printf("=== the clamp keeps every command inside the rated window ===\n");
    check(PA_GATE_BIAS_ClampCode(0) == PA_GATE_BIAS_CODE_MIN, "code 0 clamps to the floor (never 0)");
    check(PA_GATE_BIAS_ClampCode(37) == PA_GATE_BIAS_CODE_MIN, "just below the floor clamps up");
    check(PA_GATE_BIAS_ClampCode(38) == 38, "the floor itself passes through");
    check(PA_GATE_BIAS_ClampCode(200) == 200, "inside the window passes through");
    check(PA_GATE_BIAS_ClampCode(300) == PA_GATE_BIAS_CODE_MAX, "above full scale clamps down");
    bool in_window = true;
    for (int c = -500; c < 1000; c++) {
        int x = PA_GATE_BIAS_ClampCode(c);
        if (x < PA_GATE_BIAS_CODE_MIN || x > PA_GATE_BIAS_CODE_MAX) in_window = false;
        if (PA_GATE_BIAS_VggFromCode(x) > PA_GATE_BIAS_VGG_MAX_V + 0.02f) in_window = false;
    }
    check(in_window, "no input can produce a gate more positive than the rated edge");

    printf("=== clear-code contract (what CLR does during an emergency stop) ===\n");
    check(!PA_GATE_BIAS_ClearCodeIsFailSafe(PA_GATE_BIAS_CLEAR_CODE_ZERO), "zero-scale is refused");
    check(!PA_GATE_BIAS_ClearCodeIsFailSafe(PA_GATE_BIAS_CLEAR_CODE_NOP), "retain-last is refused");
    check(PA_GATE_BIAS_ClearCodeIsFailSafe(PA_GATE_BIAS_CLEAR_CODE_MID), "mid-scale is accepted");
    check(PA_GATE_BIAS_ClearCodeIsFailSafe(PA_GATE_BIAS_CLEAR_CODE_FULL), "full-scale is accepted");
    check(PA_GATE_BIAS_VggFromCode(PA_GATE_BIAS_CODE_OFF) < PA_GATE_BIAS_VGG_MAX_V,
          "the accepted clear state is below the rated gate edge (i.e. off)");

    printf("=== code <-> voltage round trip ===\n");
    bool rt = true;
    for (int c = PA_GATE_BIAS_CODE_MIN; c <= PA_GATE_BIAS_CODE_MAX; c++)
        if (PA_GATE_BIAS_CodeFromVgg(PA_GATE_BIAS_VggFromCode(c)) != c) rt = false;
    check(rt, "every code survives a round trip");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
