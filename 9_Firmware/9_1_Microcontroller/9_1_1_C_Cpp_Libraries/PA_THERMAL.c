/**
 * @file    PA_THERMAL.c
 * @brief   The PA array's thermal budget.  See PA_THERMAL.h.
 */
#include "PA_THERMAL.h"

float PA_Thermal_ChannelTemp(float case_c, float pdiss_w)
{
    return case_c + PA_THETA_JC_C_PER_W * pdiss_w;
}

bool PA_Thermal_IsSafe(float case_c, float pdiss_w)
{
    return PA_Thermal_ChannelTemp(case_c, pdiss_w) <= PA_TCH_MAX_C;
}

#include <math.h>
#include <stddef.h>

bool PA_Thermal_NtcToCelsius(float ratio, float *celsius_out)
{
    if (celsius_out == NULL || !(ratio > 0.0f) || ratio >= PA_NTC_MAX_RATIO) {
        return false;                       /* open, shorted, or nonsense */
    }
    /* V = Vref * Rntc / (Rpu + Rntc)  =>  Rntc = Rpu * ratio / (1 - ratio) */
    const float r_ntc = PA_NTC_PULLUP_OHM * ratio / (1.0f - ratio);
    /* 1/T = 1/T25 + (1/B) * ln(R/R25) */
    const float inv_t = 1.0f / PA_NTC_T25_K + (1.0f / PA_NTC_B_K) * logf(r_ntc / PA_NTC_R25_OHM);
    if (inv_t <= 0.0f) {
        return false;
    }
    *celsius_out = 1.0f / inv_t - 273.15f;
    return true;
}
