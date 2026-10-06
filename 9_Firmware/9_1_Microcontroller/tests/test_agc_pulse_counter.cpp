// test_agc_pulse_counter.cpp -- unit tests for the DIG_7 magnitude pulse counter
//
// The counter is the MCU half of WP4.2: the FPGA sends 0..7 pulses per frame,
// one per severity class, and the AGC turns that class into an attack size.  The
// failure paths matter as much as the happy path, because the link can be
// enabled in the FPGA before the MCU interrupt is wired:
//
//   no pulses        -> 0     -> AGC falls back to its fixed attack
//   more than 7      -> 0xFF  -> treated as unusable, also falls back
//
// Build: see the Makefile (header-only, no mocks needed).

#include <cassert>
#include <cstdio>

#include "AgcPulseCounter.h"

static int tests_passed = 0;
static int tests_total = 0;

#define RUN_TEST(fn) do {                        \
    printf("  [TEST] %-58s ", #fn);              \
    tests_total++;                               \
    fn();                                        \
    tests_passed++;                              \
    printf("PASS\n");                            \
} while (0)

// ---------------------------------------------------------------------------
static void test_counts_edges()
{
    AgcPulseCounter c;
    for (uint8_t i = 1; i <= 7; ++i) {
        c.onEdge();
        assert(c.peek() == i);
    }
    assert(c.consume() == 7);
}

static void test_consume_resets_for_the_next_frame()
{
    AgcPulseCounter c;
    c.onEdge();
    c.onEdge();
    assert(c.consume() == 2);
    assert(c.consume() == 0);       // second read in the same frame sees nothing
    assert(c.peek() == 0);

    c.onEdge();
    assert(c.consume() == 1);       // and the next frame counts again
}

static void test_no_pulses_reports_zero()
{
    AgcPulseCounter c;
    assert(c.consume() == 0);       // what the AGC treats as "no information"
}

static void test_too_many_edges_is_unusable()
{
    AgcPulseCounter c;
    for (uint8_t i = 0; i < 8; ++i)
        c.onEdge();
    assert(c.consume() == AgcPulseCounter::kUnknown);

    // exactly the maximum is still trustworthy
    AgcPulseCounter d;
    for (uint8_t i = 0; i < AgcPulseCounter::kMaxPulses; ++i)
        d.onEdge();
    assert(d.consume() == AgcPulseCounter::kMaxPulses);
}

static void test_noise_burst_is_unusable()
{
    AgcPulseCounter c;
    for (int i = 0; i < 100; ++i)
        c.onEdge();
    assert(c.consume() == AgcPulseCounter::kUnknown);
    assert(c.peek() == 0);          // and it does not poison the next frame
}

static void test_counter_does_not_wrap()
{
    AgcPulseCounter c;
    for (int i = 0; i < 300; ++i)
        c.onEdge();
    // saturates at 255 and is still reported as unusable rather than wrapping to
    // a plausible-looking class
    assert(c.consume() == AgcPulseCounter::kUnknown);
}

static void test_reset_clears()
{
    AgcPulseCounter c;
    c.onEdge();
    c.onEdge();
    c.reset();
    assert(c.peek() == 0);
    assert(c.consume() == 0);
}

int main()
{
    printf("=== AGC Magnitude Pulse Counter Unit Tests ===\n");
    RUN_TEST(test_counts_edges);
    RUN_TEST(test_consume_resets_for_the_next_frame);
    RUN_TEST(test_no_pulses_reports_zero);
    RUN_TEST(test_too_many_edges_is_unusable);
    RUN_TEST(test_noise_burst_is_unusable);
    RUN_TEST(test_counter_does_not_wrap);
    RUN_TEST(test_reset_clears);
    printf("=== Results: %d/%d passed ===\n", tests_passed, tests_total);
    return (tests_passed == tests_total) ? 0 : 1;
}
