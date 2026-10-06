// AgcPulseCounter.h -- counts the FPGA's AGC magnitude pulses (WP4.2)
//
// The FPGA drives DIG_7 (STM32 PD15) as a pulse train: after each frame boundary
// it emits one pulse per severity class of that frame's saturation count (see
// agc_magnitude_link.v).  The class is roughly log2 of how many samples clipped:
//
//   class   1    2    3    4     5     6      7
//   clips   1   2-3  4-7  8-15  16-31 32-63 64-255
//
// Those pulses are very short (1 us high, 3 us period, all within ~21 us of the
// frame start), so they cannot be sampled from the per-frame main loop -- they
// must be counted by an edge interrupt.  This class is deliberately pure logic
// with no HAL dependency: the ISR only calls onEdge(), so the counting and the
// failure handling are unit-testable.
//
// Failure behaviour matters more than the happy path here:
//   * no pulses  -> consume() returns 0, which the AGC treats as "no magnitude
//                   information" and falls back to its previous fixed attack.
//   * too many   -> the line is producing more edges than the encoder can emit
//                   (noise, or PD15 configured as an input without the
//                   interrupt, picking up crosstalk), so the result is
//                   kUnknown and must not be trusted either.
// That way the link can be enabled in the FPGA before it has been validated on
// hardware without changing behaviour for the worse.

#ifndef AGC_PULSE_COUNTER_H
#define AGC_PULSE_COUNTER_H

#include <stdint.h>

class AgcPulseCounter {
public:
    static const uint8_t kUnknown = 0xFF;      // magnitude not usable
    static const uint8_t kMaxPulses = 7;       // mirrors MAX_PULSES in the RTL

    // Called from the DIG_7 edge interrupt.
    void onEdge()
    {
        if (count_ < 0xFFu)
            count_++;
    }

    // Called once per frame, before the AGC decision: returns the severity class
    // (1..kMaxPulses), 0 when no pulses arrived, or kUnknown when the edge count
    // exceeded what the encoder can produce.  Resets the counter for the frame.
    uint8_t consume()
    {
        uint8_t n = count_;
        count_ = 0;
        if (n > kMaxPulses)
            return kUnknown;
        return n;
    }

    void reset() { count_ = 0; }
    uint8_t peek() const { return count_; }

private:
    volatile uint8_t count_ = 0;   // written by the ISR, read once per frame
};

#endif // AGC_PULSE_COUNTER_H
