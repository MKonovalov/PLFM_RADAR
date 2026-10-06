/**
 * @file    RAIL_FAULT.c
 * @brief   Rail-good monitoring: sample, latch, report.  See RAIL_FAULT.h.
 */
#include "RAIL_FAULT.h"

bool RailFault_Init(RailFault_Monitor_t *mon, const RailFault_Source_t *sources, uint8_t count)
{
    if (mon == NULL || sources == NULL || count == 0 || count > 32) {
        return false;
    }
    mon->sources = sources;
    mon->count = count;
    mon->latched = 0;
    mon->current = 0;
    return true;
}

bool RailFault_Sample(RailFault_Monitor_t *mon)
{
    if (mon == NULL || mon->sources == NULL) {
        return false;
    }
    uint32_t now = 0;
    for (uint8_t i = 0; i < mon->count; i++) {
        const RailFault_Source_t *src = &mon->sources[i];
        if (src->is_ok == NULL || !src->is_ok()) {
            now |= (uint32_t)1u << i;      /* a missing source counts as a fault, not as good */
        }
    }
    mon->current = now;
    mon->latched |= now;
    return now != 0;
}

bool RailFault_HasLatched(const RailFault_Monitor_t *mon)
{
    return mon != NULL && mon->latched != 0;
}

void RailFault_ClearLatch(RailFault_Monitor_t *mon)
{
    if (mon != NULL) {
        mon->latched = 0;
    }
}
