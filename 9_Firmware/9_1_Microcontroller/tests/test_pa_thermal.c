/* The PA thermal budget (issue #5).
 *
 * The numbers come from the QPA2962 brief, so the test's job is to check that the model
 * reproduces the brief's own row: at T_BASE = 85 degC and P_DISS = 36.96 W the channel must come
 * out at the stated 189 degC.  If that ever fails, the model - not the datasheet - is wrong.
 */
#include "PA_THERMAL.h"
#include <stdio.h>

static int checks, failures;
static void check(int c, const char *what)
{
    checks++;
    if (!c) { failures++; printf("   FAIL: %s\n", what); }
}
static int close_to(float a, float b, float tol) { float d = a - b; if (d < 0) d = -d; return d <= tol; }

int main(void)
{
    printf("=== the model reproduces the brief's own row ===\n");
    check(close_to(PA_Thermal_ChannelTemp(85.0f, PA_PDISS_QUIESCENT_W), 189.0f, 1.0f),
          "85 degC case at 36.96 W gives the brief's 189 degC channel");

    printf("=== the derived case limit ===\n");
    printf("      PA_TCASE_MAX_C = %.1f degC\n", (double)PA_TCASE_MAX_C);
    check(PA_TCASE_MAX_C > 90.0f && PA_TCASE_MAX_C < 100.0f,
          "the case limit lands near 95 degC, not somewhere arbitrary");
    check(PA_Thermal_IsSafe(PA_TCASE_MAX_C - 1.0f, PA_PDISS_QUIESCENT_W), "just inside is safe");
    check(!PA_Thermal_IsSafe(PA_TCASE_MAX_C + 1.0f, PA_PDISS_QUIESCENT_W), "just outside is not");

    printf("=== the firmware's existing 75 degC limit is inside the physics ===\n");
    check(PA_Thermal_IsSafe(75.0f, PA_PDISS_QUIESCENT_W),
          "the 75 degC PA sensor limit leaves the channel under 200 degC");
    check(PA_Thermal_ChannelTemp(75.0f, PA_PDISS_QUIESCENT_W) < 185.0f,
          "and it lands around 180 degC, so the limit is conservative rather than marginal");

    printf("=== driven dissipation is the easier case ===\n");
    check(PA_PDISS_DRIVEN_W < PA_PDISS_QUIESCENT_W,
          "under RF less is dissipated than at quiescent, so planning with quiescent is safe");

    printf("=== the NTC probes on the eight channels (issue #5) ===\n");
    {
        float c;
        /* The pull-up equals R25, so the divider midpoint is exactly 25 degC - which makes the
         * ratio 0.5 a self-check of the whole conversion, not just of the arithmetic. */
        check(PA_Thermal_NtcToCelsius(0.5f, &c) && close_to(c, 25.0f, 0.05f),
              "a half-ratio divider reads 25 degC");
        /* colder -> the NTC resistance rises -> the node sits higher */
        check(PA_Thermal_NtcToCelsius(0.6f, &c) && c < 25.0f, "a higher ratio is colder");
        check(PA_Thermal_NtcToCelsius(0.9f, &c) && c < 0.0f, "0.9 is below freezing");
        /* hotter -> less resistance -> lower node */
        check(PA_Thermal_NtcToCelsius(0.1f, &c) && c > 85.0f && c < 105.0f,
              "0.1 lands near the derived 95 degC case limit");
        check(PA_Thermal_NtcToCelsius(0.3f, &c) && c > 40.0f && c < 60.0f, "0.3 is hot but sane");

        printf("      0.10 -> %.1f C   0.30 -> %.1f C   0.50 -> %.1f C   0.90 -> %.1f C\n",
               (double)(PA_Thermal_NtcToCelsius(0.10f, &c) ? c : -999),
               (double)(PA_Thermal_NtcToCelsius(0.30f, &c) ? c : -999),
               (double)(PA_Thermal_NtcToCelsius(0.50f, &c) ? c : -999),
               (double)(PA_Thermal_NtcToCelsius(0.90f, &c) ? c : -999));

        /* an unplugged probe sits on the rail and must never read as cold */
        check(!PA_Thermal_NtcToCelsius(1.0f, &c), "a probe on the rail is rejected, not read as cold");
        check(!PA_Thermal_NtcToCelsius(0.996f, &c), "just past the limit is rejected too");
        check(!PA_Thermal_NtcToCelsius(0.0f, &c), "a shorted probe is rejected");
        check(!PA_Thermal_NtcToCelsius(-0.1f, &c), "a negative ratio is rejected");
        check(!PA_Thermal_NtcToCelsius(0.5f, NULL), "a NULL output is refused");
        /* and it must be monotonic, since the protection depends on that */
        {
            float prev = 1e9f; int mono = 1;   /* the sequence falls, so start above every value */
            for (int i = 1; i < 99; i++) {
                float t;
                if (!PA_Thermal_NtcToCelsius((float)i / 100.0f, &t)) { mono = 0; break; }
                if (t > prev) { mono = 0; break; }   /* hotter as the ratio rises = wrong */
                prev = t;
            }
            check(mono, "temperature falls monotonically as the ratio rises");
        }
    }

    printf("=== cold is cold ===\n");
    check(close_to(PA_Thermal_ChannelTemp(-40.0f, 0.0f), -40.0f, 0.001f),
          "no dissipation means no rise");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
