/*
 * #5: does the soak recorder actually distinguish a settled run from one still climbing?
 *
 * That is the whole point of recording the rise rather than the endpoint, so the test drives two
 * synthetic curves that share a final temperature: one that settles, one still climbing at the end.
 * A single-reading implementation would call both of them identical.
 */
#include <stdio.h>
#include <stdbool.h>
#include <math.h>

#include "PA_SOAK.h"

static int checks, failures;
static void check(int c, const char *what)
{
    checks++;
    if (!c) { failures++; printf("  FAIL: %s\n", what); }
    else    { printf("  ok  : %s\n", what); }
}

/* an exponential approach to a plateau: T(t) = start + (plateau - start) * (1 - exp(-t/tau)) */
static void curve(float *out, int n, float start, float plateau, float tau_s, float t_s)
{
    const float k = 1.0f - expf(-t_s / tau_s);
    for (int i = 0; i < n; i++) {
        out[i] = start + (plateau - start) * k;
    }
}

int main(void)
{
    float t[PA_SOAK_CHANNELS];

    printf("=== #5: the soak recorder, against two curves with the SAME endpoint ===\n");

    /* ---- a run that settles: tau 300 s over a 1800 s window */
    PaSoak_t settled;
    curve(t, PA_SOAK_CHANNELS, 25.0f, 68.0f, 300.0f, 0.0f);
    PaSoak_Start(&settled, 0u, t, PA_SOAK_CHANNELS);
    for (uint32_t ms = 30000u; ms <= 1800000u; ms += 30000u) {   /* every 30 s for 30 min */
        curve(t, PA_SOAK_CHANNELS, 25.0f, 68.0f, 300.0f, (float)ms / 1000.0f);
        PaSoak_Update(&settled, ms, t, PA_SOAK_CHANNELS);
    }
    printf("      settled run:  first %.1f, peak %.1f, rise %.1f C, peak at %.0f%% of the window\n",
           (double)settled.ch[0].first_c, (double)settled.ch[0].peak_c,
           (double)PaSoak_RiseC(&settled, 0),
           (double)(PaSoak_PeakPosition(&settled, 0) * 100.0f));

    check(settled.samples == 61, "61 samples over the 30-minute window at 30 s intervals");
    check(fabsf(PaSoak_RiseC(&settled, 0) - 43.0f) < 0.5f, "the rise is recorded (about 43 K here)");
    check(PaSoak_HasSettled(&settled), "a run that has stopped moving is reported as SETTLED");
    check(PaSoak_LastRiseC(&settled, 0) / 0.5f < PA_SOAK_SETTLED_C_PER_MIN,
          "...because its rate at the end is under the settling threshold");
    check(fabsf(settled.ch[0].peak_c - 68.0f) < 0.9f, "the peak reaches the plateau it was driven to");

    /* ---- a run still climbing, chosen to pass through the SAME 68 C at the end of the window:
     * tau 1800 s and a 93 C plateau put it at 25 + 68 * (1 - exp(-1)) = 68.0 C at t = 1800 s.  So the
     * two runs are indistinguishable from their final temperature and differ only in whether they
     * have stopped moving. */
    PaSoak_t climbing;
    curve(t, PA_SOAK_CHANNELS, 25.0f, 93.0f, 1800.0f, 0.0f);
    PaSoak_Start(&climbing, 0u, t, PA_SOAK_CHANNELS);
    for (uint32_t ms = 30000u; ms <= 1800000u; ms += 30000u) {
        curve(t, PA_SOAK_CHANNELS, 25.0f, 93.0f, 1800.0f, (float)ms / 1000.0f);
        PaSoak_Update(&climbing, ms, t, PA_SOAK_CHANNELS);
    }
    printf("      climbing run: first %.1f, peak %.1f, rise %.1f C, peak at %.0f%% of the window\n",
           (double)climbing.ch[0].first_c, (double)climbing.ch[0].peak_c,
           (double)PaSoak_RiseC(&climbing, 0),
           (double)(PaSoak_PeakPosition(&climbing, 0) * 100.0f));

    check(!PaSoak_HasSettled(&climbing),
          "a run still climbing at the end is NOT reported as settled - the thing an endpoint hides");
    check(PaSoak_LastRiseC(&climbing, 0) / 0.5f > PA_SOAK_SETTLED_C_PER_MIN,
          "...because its rate at the end is still above the settling threshold");
    /* and the two are genuinely indistinguishable from their last reading alone */
    check(fabsf(settled.ch[0].last_c - climbing.ch[0].last_c) < 0.15f,
          "the two runs end at the SAME temperature - only the rate tells them apart");
    printf("      end at %.2f vs %.2f C; final rate %.3f vs %.3f C/min\n",
           (double)settled.ch[0].last_c, (double)climbing.ch[0].last_c,
           (double)(PaSoak_LastRiseC(&settled, 0) / 0.5f),
           (double)(PaSoak_LastRiseC(&climbing, 0) / 0.5f));

    /* ---- the hottest channel */
    printf("=== channel bookkeeping ===\n");
    PaSoak_t multi;
    float m[PA_SOAK_CHANNELS] = { 30, 40, 50, 60, 70, 80, 90, 20 };
    PaSoak_Start(&multi, 0u, m, PA_SOAK_CHANNELS);
    check(PaSoak_HottestChannel(&multi) == 6, "the hottest channel is identified");
    for (int i = 0; i < PA_SOAK_CHANNELS; i++) { m[i] += (float)i; }
    PaSoak_Update(&multi, 60000u, m, PA_SOAK_CHANNELS);
    check(PaSoak_RiseC(&multi, 7) > 6.9f && PaSoak_RiseC(&multi, 0) < 0.1f,
          "the rise is per channel, not global");
    check(PaSoak_RiseC(&multi, -1) == 0.0f && PaSoak_RiseC(&multi, 99) == 0.0f,
          "an out-of-range channel is handled");

    /* ---- edge cases */
    printf("=== edge cases ===\n");
    PaSoak_t fresh;
    PaSoak_Start(&fresh, 0u, m, PA_SOAK_CHANNELS);
    check(PaSoak_RiseC(&fresh, 0) == 0.0f, "one sample gives no rise - it is not a rise yet");
    check(!PaSoak_HasSettled(&fresh), "and cannot be called settled either");
    check(PaSoak_HottestChannel(&fresh) >= 0, "but the current hottest channel is still known");

    PaSoak_Start(NULL, 0u, m, PA_SOAK_CHANNELS);
    check(1, "a null recorder does not crash");

    PaSoak_t nulls;
    PaSoak_Start(&nulls, 0u, NULL, PA_SOAK_CHANNELS);
    PaSoak_Update(&nulls, 1000u, NULL, PA_SOAK_CHANNELS);
    check(nulls.samples == 0 && PaSoak_RiseC(&nulls, 0) == 0.0f, "a null sample array is ignored");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
