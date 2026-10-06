// test_pa_sense.cpp -- unit tests for the PA current-sense chain constants
//
// Regression cover for the feasibility-review findings M4/M5: the shunt value,
// the INA241 gain, the ADS7830 reference and the firmware scaling constant are
// now one file (PA_SENSE.h).  These tests pin the resulting numbers so that any
// future drift between schematic, BOM and firmware fails the build or the test.
//
// Build: c++ -std=c++17 ... (see Makefile)

#include <cassert>
#include <cstdio>
#include <cmath>

#include "PA_SENSE.h"

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
// Derived chain values
// ---------------------------------------------------------------------------
static void test_full_scale_reachable()
{
    // 2.5 V / (5 mOhm * 50) = 10 A of measurable range.
    assert(fabsf(PA_SENSE_FULL_SCALE_A - 10.0f) < 0.05f);
    // The over-current threshold must sit well inside that range or the ADC
    // saturates before protection can ever trigger (the failure mode the
    // 0.1 ohm BOM part sets up: full scale would be 0.5 A).
    assert(PA_SENSE_FULL_SCALE_A > PA_IDQ_OC_TRIP_A * 1.5f);
    // ...and the bias target must be measurable with margin.
    assert(PA_IDQ_TARGET_A < PA_SENSE_FULL_SCALE_A * 0.5f);
}

static void test_amps_per_lsb()
{
    // 10 A over 255 codes = 39.2 mA/LSB.  Documenting the resolution limit of
    // the A3 + 5 mOhm combination is part of the fix (Rev B moves to 100 V/V).
    assert(fabsf(PA_SENSE_AMPS_PER_LSB - 0.0392f) < 0.002f);
}

static void test_bias_target_code()
{
    // 1.680 A -> 0.42 V -> code 43 with a 2.5 V reference (was read as code 32
    // and scaled with 3.3 V before the fix, i.e. 32 % low current in reality).
    uint8_t code = paSenseAmpsToCode(PA_IDQ_TARGET_A);
    assert(code >= 42 && code <= 44);
    assert(fabsf(paSenseCodeToAmps(code) - PA_IDQ_TARGET_A) < PA_SENSE_AMPS_PER_LSB);
}

static void test_over_current_code_in_range()
{
    uint8_t code = paSenseAmpsToCode(PA_IDQ_OC_TRIP_A);
    assert(code > 0 && code < 255);          // reachable, not clipped
    float back = paSenseCodeToAmps(code);
    assert(fabsf(back - PA_IDQ_OC_TRIP_A) < 2.0f * PA_SENSE_AMPS_PER_LSB);
}

static void test_code_scaling_is_monotonic()
{
    uint8_t prev = 0;
    for (int c = 1; c <= 255; ++c) {
        uint8_t code = paSenseAmpsToCode(PA_SENSE_FULL_SCALE_A * (float)c / 255.0f);
        assert(code >= prev);
        prev = code;
    }
    assert(paSenseAmpsToCode(0.0f) == 0);
    assert(paSenseAmpsToCode(-1.0f) == 0);                 // clamped low
    assert(paSenseAmpsToCode(100.0f) == 255);              // clamped high
}

static void test_wrong_shunt_is_detectable()
{
    // Documentation-by-test: with the 0.1 ohm part currently named in the PA
    // BOM the full scale is 0.5 A -- below the 2.5 A protection threshold.  The
    // static_assert in PA_SENSE.h makes that configuration uncompilable; this
    // test records the arithmetic so the reason is not lost.
    const float bad_full_scale = ADS7830_VREF_VOLTS / (0.1f * PA_INA_GAIN);
    assert(bad_full_scale < PA_IDQ_OC_TRIP_A);
    assert(fabsf(bad_full_scale - 0.5f) < 0.01f);
}

int main()
{
    printf("=== PA Current-Sense Chain Unit Tests ===\n");
    RUN_TEST(test_full_scale_reachable);
    RUN_TEST(test_amps_per_lsb);
    RUN_TEST(test_bias_target_code);
    RUN_TEST(test_over_current_code_in_range);
    RUN_TEST(test_code_scaling_is_monotonic);
    RUN_TEST(test_wrong_shunt_is_detectable);
    printf("=== Results: %d/%d passed ===\n", tests_passed, tests_total);
    return (tests_passed == tests_total) ? 0 : 1;
}
