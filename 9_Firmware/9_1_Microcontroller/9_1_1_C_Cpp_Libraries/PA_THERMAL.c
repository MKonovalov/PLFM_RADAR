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
