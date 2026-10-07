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

    printf("=== cold is cold ===\n");
    check(close_to(PA_Thermal_ChannelTemp(-40.0f, 0.0f), -40.0f, 0.001f),
          "no dissipation means no rise");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
