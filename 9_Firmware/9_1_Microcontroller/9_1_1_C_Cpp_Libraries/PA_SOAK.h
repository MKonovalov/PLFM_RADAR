/**
 * @file    PA_SOAK.h
 * @brief   Record the thermal RISE, not just the endpoint (issue #5).
 *
 * The acceptance for #5 asks for a "30-minute soak at IDQ, 10 % duty, baseplate <= 85 degC with
 * margin; record the rise, not just the endpoint".  A soak that reports only the final temperature
 * cannot distinguish a system that has settled from one still climbing when the clock ran out - and
 * those look identical in a single reading.
 *
 * So this keeps, per channel, the first reading, the peak, when the peak happened, and the rise
 * between them.  A run whose peak arrives at the end of the window is still climbing; one whose peak
 * arrives early has settled.  Both are visible in one line of telemetry.
 *
 * It is deliberately passive: it is fed samples and does no I/O, so the same code can be exercised
 * against a synthetic curve in the test and against the real sensors on the bench.
 */
#ifndef PA_SOAK_H
#define PA_SOAK_H

#include <stdbool.h>
#include <stdint.h>

/** The firmware carries eight PA/baseplate sensors. */
#define PA_SOAK_CHANNELS 8

typedef struct {
    bool     valid;          /**< at least one sample has been taken */
    float    first_c;
    float    peak_c;
    uint32_t peak_at_ms;     /**< when the peak was seen, relative to the start */
    float    prev_c;         /**< the reading before the last one */
    float    last_c;
} PaSoakChannel_t;

typedef struct {
    bool            running;
    uint32_t        started_ms;
    uint32_t        prev_ms;    /**< the timestamp of the previous sample, for the rate */
    uint32_t        last_ms;
    uint32_t        samples;
    PaSoakChannel_t ch[PA_SOAK_CHANNELS];
} PaSoak_t;

/** Begin a soak.  `temps` may be NULL, in which case the first sample comes from PaSoak_Update(). */
void PaSoak_Start(PaSoak_t *s, uint32_t now_ms, const float *temps, int n);

/**
 * Feed one reading.  A channel whose reading is above its peak moves the peak, so the peak time is
 * the time of the highest reading rather than the time of the last one.
 */
void PaSoak_Update(PaSoak_t *s, uint32_t now_ms, const float *temps, int n);

/** The rise seen on a channel: peak minus first.  0 if fewer than two samples. */
float PaSoak_RiseC(const PaSoak_t *s, int channel);

/** When the peak arrived, as a fraction of the elapsed window. */
float PaSoak_PeakPosition(const PaSoak_t *s, int channel);

/** The rise over the LAST sampling interval - how fast the channel is still moving. */
float PaSoak_LastRiseC(const PaSoak_t *s, int channel);

/**
 * True if every channel has stopped moving, in ABSOLUTE terms: its rate over the final interval is
 * below PA_SOAK_SETTLED_C_PER_MIN.
 *
 * Two earlier attempts at this were wrong, and the test showed both:
 *
 *   * looking at when the peak arrived - a soak rises monotonically toward its plateau, so every
 *     sample is a new peak and the peak position is always 100 %;
 *   * looking at the final interval's rise as a FRACTION of the run's own total - a run whose time
 *     constant equals the window moves only 1 % of its total in the last interval, so any relative
 *     threshold calls it settled.  A relative threshold cannot separate a slow climb from a settle,
 *     and a thermal soak is precisely a slow climb.
 *
 * What separates them is the rate in absolute terms.
 */
bool PaSoak_HasSettled(const PaSoak_t *s);

/**
 * The settling threshold, in degrees per minute.  This is a REQUIREMENT, not a measurement: a run
 * that is still moving faster than this at the end of the window has not demonstrated that it has
 * stopped, whatever its endpoint looks like.  0.1 C/min leaves under 1 C of un-recorded climb across
 * a 10-minute tail, which is well inside the margin the acceptance asks for.
 */
#define PA_SOAK_SETTLED_C_PER_MIN 0.1f

/** The hottest channel, or -1 if none is valid. */
int PaSoak_HottestChannel(const PaSoak_t *s);

#endif /* PA_SOAK_H */
