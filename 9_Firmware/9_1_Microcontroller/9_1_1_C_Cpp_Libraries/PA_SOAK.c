/**
 * @file    PA_SOAK.c
 * @brief   See the header: record the rise, not just the endpoint (issue #5).
 */
#include "PA_SOAK.h"

static void soak_note(PaSoak_t *s, int i, float t, uint32_t now_ms)
{
    PaSoakChannel_t *c = &s->ch[i];
    if (!c->valid) {
        c->valid    = true;
        c->first_c  = t;
        c->peak_c   = t;
        c->peak_at_ms = now_ms - s->started_ms;
        c->prev_c   = t;
        c->last_c   = t;
        return;
    }
    c->prev_c = c->last_c;
    c->last_c = t;
    if (t > c->peak_c) {
        c->peak_c = t;
        c->peak_at_ms = now_ms - s->started_ms;
    }
}

void PaSoak_Start(PaSoak_t *s, uint32_t now_ms, const float *temps, int n)
{
    if (!s) {
        return;
    }
    *s = (PaSoak_t){0};
    s->running    = true;
    s->started_ms = now_ms;
    s->last_ms    = now_ms;
    if (temps && n > 0) {
        PaSoak_Update(s, now_ms, temps, n);
    }
}

void PaSoak_Update(PaSoak_t *s, uint32_t now_ms, const float *temps, int n)
{
    if (!s || !temps || n <= 0) {
        return;
    }
    s->prev_ms = s->last_ms;
    s->last_ms = now_ms;
    s->samples++;
    const int lim = (n < PA_SOAK_CHANNELS) ? n : PA_SOAK_CHANNELS;
    for (int i = 0; i < lim; i++) {
        soak_note(s, i, temps[i], now_ms);
    }
}

float PaSoak_RiseC(const PaSoak_t *s, int channel)
{
    if (!s || channel < 0 || channel >= PA_SOAK_CHANNELS) {
        return 0.0f;
    }
    const PaSoakChannel_t *c = &s->ch[channel];
    if (!c->valid || s->samples < 2) {
        return 0.0f;
    }
    return c->peak_c - c->first_c;
}

float PaSoak_PeakPosition(const PaSoak_t *s, int channel)
{
    if (!s || channel < 0 || channel >= PA_SOAK_CHANNELS) {
        return 0.0f;
    }
    const PaSoakChannel_t *c = &s->ch[channel];
    const uint32_t elapsed = s->last_ms - s->started_ms;
    if (!c->valid || elapsed == 0u) {
        return 0.0f;
    }
    return (float)c->peak_at_ms / (float)elapsed;
}

float PaSoak_LastRiseC(const PaSoak_t *s, int channel)
{
    if (!s || channel < 0 || channel >= PA_SOAK_CHANNELS) {
        return 0.0f;
    }
    const PaSoakChannel_t *c = &s->ch[channel];
    if (!c->valid || s->samples < 3) {
        return 0.0f;   /* one interval is not a rate */
    }
    return c->last_c - c->prev_c;
}

bool PaSoak_HasSettled(const PaSoak_t *s)
{
    if (!s || s->samples < 3 || s->last_ms <= s->started_ms) {
        return false;
    }
    const uint32_t interval_ms = s->last_ms - s->prev_ms;
    if (interval_ms == 0u) {
        return false;                       /* no interval, no rate */
    }
    const float interval_min = (float)interval_ms / 60000.0f;

    /* Still climbing at the end of the window is the failure mode a single endpoint reading hides. */
    for (int i = 0; i < PA_SOAK_CHANNELS; i++) {
        if (!s->ch[i].valid) {
            continue;
        }
        const float last = PaSoak_LastRiseC(s, i);
        if (last < 0.0f) {
            return false;                   /* cooling at the end is its own problem */
        }
        if (last / interval_min > PA_SOAK_SETTLED_C_PER_MIN) {
            return false;
        }
    }
    return true;
}

int PaSoak_HottestChannel(const PaSoak_t *s)
{
    if (!s) {
        return -1;
    }
    int best = -1;
    for (int i = 0; i < PA_SOAK_CHANNELS; i++) {
        if (!s->ch[i].valid) {
            continue;
        }
        if (best < 0 || s->ch[i].peak_c > s->ch[best].peak_c) {
            best = i;
        }
    }
    return best;
}
