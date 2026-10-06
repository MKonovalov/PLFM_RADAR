// test_agc_outer_loop.cpp -- C++ unit tests for ADAR1000_AGC outer-loop AGC
//
// Tests the STM32 outer-loop AGC class that adjusts ADAR1000 VGA gain based
// on the FPGA's saturation flag.  Uses the existing HAL mock/spy framework.
//
// Build: c++ -std=c++17 ... (see Makefile TESTS_WITH_CXX rule)

#include <cassert>
#include <cstdio>
#include <cstring>

// Shim headers override real STM32/diag headers
#include "stm32_hal_mock.h"
#include "ADAR1000_AGC.h"
#include "ADAR1000_Manager.h"

// ---------------------------------------------------------------------------
// Linker symbols required by ADAR1000_Manager.cpp (pulled in via main.h shim)
// ---------------------------------------------------------------------------
uint8_t GUI_start_flag_received = 0;
uint8_t USB_Buffer[64] = {0};
extern "C" void Error_Handler(void) {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int tests_passed = 0;
static int tests_total  = 0;

#define RUN_TEST(fn)                                                           \
    do {                                                                        \
        tests_total++;                                                          \
        printf("  [%2d] %-55s ", tests_total, #fn);                            \
        fn();                                                                   \
        tests_passed++;                                                         \
        printf("PASS\n");                                                       \
    } while (0)

// ---------------------------------------------------------------------------
// Test 1: Default construction matches design spec
// ---------------------------------------------------------------------------
static void test_defaults()
{
    ADAR1000_AGC agc;

    assert(agc.agc_base_gain == 30);  // kDefaultRxVgaGain
    assert(agc.gain_step_down == 4);
    assert(agc.gain_step_up == 2);   // Phase 1: was 1 -> ~31 s blind window
    assert(agc.min_gain == 0);
    assert(agc.max_gain == 127);
    assert(agc.holdoff_frames == 1); // Phase 1: was 4
    assert(agc.enabled == false);  // disabled by default — FPGA DIG_6 is source of truth
    assert(agc.holdoff_counter == 0);
    assert(agc.last_saturated == false);
    assert(agc.saturation_event_count == 0);

    // All cal offsets zero
    for (int i = 0; i < AGC_TOTAL_CHANNELS; ++i) {
        assert(agc.cal_offset[i] == 0);
    }
}

// ---------------------------------------------------------------------------
// Test 2: Saturation reduces gain by step_down
// ---------------------------------------------------------------------------
static void test_saturation_reduces_gain()
{
    ADAR1000_AGC agc;
    agc.enabled = true;  // default is OFF; enable for this test
    uint8_t initial = agc.agc_base_gain;  // 30

    agc.update(true);  // saturation

    assert(agc.agc_base_gain == initial - agc.gain_step_down);  // 26
    assert(agc.last_saturated == true);
    assert(agc.holdoff_counter == 0);
}

// ---------------------------------------------------------------------------
// Test 3: Holdoff prevents premature gain-up
// ---------------------------------------------------------------------------
static void test_holdoff_prevents_early_gain_up()
{
    ADAR1000_AGC agc;
    agc.enabled = true;  // default is OFF; enable for this test
    agc.update(true);  // saturate once -> gain = 26
    uint8_t after_sat = agc.agc_base_gain;

    // Feed (holdoff_frames - 1) clear frames — should NOT increase gain
    for (uint8_t i = 0; i < agc.holdoff_frames - 1; ++i) {
        agc.update(false);
        assert(agc.agc_base_gain == after_sat);
    }

    // holdoff_counter should be holdoff_frames - 1
    assert(agc.holdoff_counter == agc.holdoff_frames - 1);
}

// ---------------------------------------------------------------------------
// Test 4: Recovery after holdoff period
// ---------------------------------------------------------------------------
static void test_recovery_after_holdoff()
{
    ADAR1000_AGC agc;
    agc.enabled = true;  // default is OFF; enable for this test
    agc.update(true);  // saturate -> gain = 26
    uint8_t after_sat = agc.agc_base_gain;

    // Feed exactly holdoff_frames clear frames
    for (uint8_t i = 0; i < agc.holdoff_frames; ++i) {
        agc.update(false);
    }

    assert(agc.agc_base_gain == after_sat + agc.gain_step_up);  // 27
    assert(agc.holdoff_counter == 0);  // reset after recovery
}

// ---------------------------------------------------------------------------
// Test 5: Min gain clamping
// ---------------------------------------------------------------------------
static void test_min_gain_clamp()
{
    ADAR1000_AGC agc;
    agc.enabled = true;  // default is OFF; enable for this test
    agc.min_gain = 10;
    agc.agc_base_gain = 12;
    agc.gain_step_down = 4;

    agc.update(true);  // 12 - 4 = 8, but min = 10
    assert(agc.agc_base_gain == 10);

    agc.update(true);  // already at min
    assert(agc.agc_base_gain == 10);
}

// ---------------------------------------------------------------------------
// Test 6: Max gain clamping
// ---------------------------------------------------------------------------
static void test_max_gain_clamp()
{
    ADAR1000_AGC agc;
    agc.enabled = true;  // default is OFF; enable for this test
    agc.max_gain = 32;
    agc.agc_base_gain = 31;
    agc.gain_step_up = 2;
    agc.holdoff_frames = 1;  // immediate recovery

    agc.update(false);  // 31 + 2 = 33, but max = 32
    assert(agc.agc_base_gain == 32);

    agc.update(false);  // already at max
    assert(agc.agc_base_gain == 32);
}

// ---------------------------------------------------------------------------
// Test 7: Per-channel calibration offsets
// ---------------------------------------------------------------------------
static void test_calibration_offsets()
{
    ADAR1000_AGC agc;
    agc.agc_base_gain = 30;
    agc.min_gain = 0;
    agc.max_gain = 60;

    agc.cal_offset[0]  =  5;   // 30 + 5  = 35
    agc.cal_offset[1]  = -10;  // 30 - 10 = 20
    agc.cal_offset[15] =  40;  // 30 + 40 = 60 (clamped to max)

    assert(agc.effectiveGain(0) == 35);
    assert(agc.effectiveGain(1) == 20);
    assert(agc.effectiveGain(15) == 60);  // clamped to max_gain

    // Negative clamp
    agc.cal_offset[2] = -50;   // 30 - 50 = -20, clamped to min_gain = 0
    assert(agc.effectiveGain(2) == 0);

    // Out-of-range index returns min_gain
    assert(agc.effectiveGain(16) == agc.min_gain);
}

// ---------------------------------------------------------------------------
// Test 8: Disabled AGC is a no-op
// ---------------------------------------------------------------------------
static void test_disabled_noop()
{
    ADAR1000_AGC agc;
    agc.enabled = false;
    uint8_t original = agc.agc_base_gain;

    agc.update(true);   // should be ignored
    assert(agc.agc_base_gain == original);
    assert(agc.last_saturated == false);  // not updated when disabled
    assert(agc.saturation_event_count == 0);

    agc.update(false);  // also ignored
    assert(agc.agc_base_gain == original);
}

// ---------------------------------------------------------------------------
// Test 9: applyGain() produces correct SPI writes
// ---------------------------------------------------------------------------
static void test_apply_gain_spi()
{
    spy_reset();

    ADAR1000Manager mgr;  // creates 4 devices
    ADAR1000_AGC agc;
    agc.agc_base_gain = 42;

    agc.applyGain(mgr);

    // Each channel: adarSetRxVgaGain -> adarWrite(gain) + adarWrite(LOAD_WORKING)
    // Each adarWrite: CS_low (GPIO_WRITE) + SPI_TRANSMIT + CS_high (GPIO_WRITE)
    // = 3 spy records per adarWrite
    // = 6 spy records per channel
    // = 16 channels * 6 = 96 total spy records

    // Verify SPI transmit count: 2 SPI calls per channel * 16 channels = 32
    int spi_count = spy_count_type(SPY_SPI_TRANSMIT);
    assert(spi_count == 32);

    // Verify GPIO write count: 4 GPIO writes per channel (CS low + CS high for each of 2 adarWrite calls)
    int gpio_writes = spy_count_type(SPY_GPIO_WRITE);
    assert(gpio_writes == 64);  // 16 ch * 2 adarWrite * 2 GPIO each
}

// ---------------------------------------------------------------------------
// Test 10: resetState() clears counters but preserves config
// ---------------------------------------------------------------------------
static void test_reset_preserves_config()
{
    ADAR1000_AGC agc;
    agc.enabled = true;  // default is OFF; enable for this test
    agc.agc_base_gain = 42;
    agc.gain_step_down = 8;
    agc.cal_offset[3] = -5;

    // Generate some state
    agc.update(true);
    agc.update(true);
    assert(agc.saturation_event_count == 2);
    assert(agc.last_saturated == true);

    agc.resetState();

    // State cleared
    assert(agc.holdoff_counter == 0);
    assert(agc.last_saturated == false);
    assert(agc.saturation_event_count == 0);

    // Config preserved
    assert(agc.agc_base_gain == 42 - 8 - 8);  // two saturations applied before reset
    assert(agc.gain_step_down == 8);
    assert(agc.cal_offset[3] == -5);
}

// ---------------------------------------------------------------------------
// Test 11: Saturation counter increments correctly
// ---------------------------------------------------------------------------
static void test_saturation_counter()
{
    ADAR1000_AGC agc;
    agc.enabled = true;  // default is OFF; enable for this test

    for (int i = 0; i < 10; ++i) {
        agc.update(true);
    }
    assert(agc.saturation_event_count == 10);

    // Clear frames don't increment saturation count
    for (int i = 0; i < 5; ++i) {
        agc.update(false);
    }
    assert(agc.saturation_event_count == 10);
}

// ---------------------------------------------------------------------------
// Test 12: Mixed saturation/clear sequence
// ---------------------------------------------------------------------------
static void test_mixed_sequence()
{
    ADAR1000_AGC agc;
    agc.enabled = true;  // default is OFF; enable for this test
    agc.agc_base_gain = 30;
    agc.gain_step_down = 4;
    agc.gain_step_up = 1;
    agc.holdoff_frames = 3;

    // Saturate: 30 -> 26
    agc.update(true);
    assert(agc.agc_base_gain == 26);
    assert(agc.holdoff_counter == 0);

    // 2 clear frames (not enough for recovery)
    agc.update(false);
    agc.update(false);
    assert(agc.agc_base_gain == 26);
    assert(agc.holdoff_counter == 2);

    // Saturate again: 26 -> 22, counter resets
    agc.update(true);
    assert(agc.agc_base_gain == 22);
    assert(agc.holdoff_counter == 0);
    assert(agc.saturation_event_count == 2);

    // 3 clear frames -> recovery: 22 -> 23
    agc.update(false);
    agc.update(false);
    agc.update(false);
    assert(agc.agc_base_gain == 23);
    assert(agc.holdoff_counter == 0);

    // 3 more clear -> 23 -> 24
    agc.update(false);
    agc.update(false);
    agc.update(false);
    assert(agc.agc_base_gain == 24);
}

// ---------------------------------------------------------------------------
// Test 13: Effective gain with edge-case base_gain values
// ---------------------------------------------------------------------------
static void test_effective_gain_edge_cases()
{
    ADAR1000_AGC agc;
    agc.min_gain = 5;
    agc.max_gain = 250;

    // Base gain at zero with positive offset
    agc.agc_base_gain = 0;
    agc.cal_offset[0] = 3;
    assert(agc.effectiveGain(0) == 5);  // 0 + 3 = 3, clamped to min_gain=5

    // Base gain at max with zero offset
    agc.agc_base_gain = 250;
    agc.cal_offset[0] = 0;
    assert(agc.effectiveGain(0) == 250);

    // Base gain at max with positive offset -> clamped
    agc.agc_base_gain = 250;
    agc.cal_offset[0] = 10;
    assert(agc.effectiveGain(0) == 250);  // clamped to max_gain
}

// ---------------------------------------------------------------------------
// validate() -- configuration guards (feasibility-review WP4.3)
// ---------------------------------------------------------------------------
static void test_validate_repairs_bad_config()
{
    ADAR1000_AGC agc;
    agc.gain_step_down = 0;        // disables the attack entirely
    agc.gain_step_up = 0;          // disables recovery
    agc.holdoff_frames = 0;        // disables the holdoff
    agc.min_gain = 40;
    agc.max_gain = 10;             // min > max
    agc.agc_base_gain = 200;       // above max

    bool changed = agc.validate();
    assert(changed);
    assert(agc.gain_step_down >= 1);
    assert(agc.gain_step_up >= 1);
    assert(agc.holdoff_frames >= 1);
    assert(agc.min_gain <= agc.max_gain);
    assert(agc.agc_base_gain >= agc.min_gain && agc.agc_base_gain <= agc.max_gain);

    // A sane configuration must not be flagged as changed.
    ADAR1000_AGC ok;
    assert(ok.validate() == false);
}

static void test_max_gain_clamped_to_vga_range()
{
    ADAR1000_AGC agc;
    agc.max_gain = 255;            // the 8th bit is the switched attenuator
    assert(agc.validate());
    assert(agc.max_gain == 127);
}

// ---------------------------------------------------------------------------
// Hard overload (DIG_7) -- larger attack step (feasibility-review WP4.2)
// ---------------------------------------------------------------------------
static void test_hard_overload_uses_larger_step()
{
    ADAR1000_AGC agc;
    agc.enabled = true;
    agc.agc_base_gain = 60;
    agc.holdoff_frames = 4;                        // keep decline out of this test

    agc.update(false, true);                       // hard flag without saturation
    assert(agc.agc_base_gain == 60);               // -> ignored, no attack
    assert(agc.hard_overload_count == 0);
    assert(agc.last_hard_overload == false);

    agc.update(true, true);                        // hard overload
    assert(agc.agc_base_gain == 60 - agc.gain_step_down_hard);
    assert(agc.hard_overload_count == 1);
    assert(agc.saturation_event_count == 1);
    assert(agc.last_hard_overload);

    agc.update(true, false);                       // soft clip -> normal step
    assert(agc.agc_base_gain == 60 - agc.gain_step_down_hard - agc.gain_step_down);
    assert(agc.hard_overload_count == 1);
    assert(agc.last_hard_overload == false);

    // Backwards-compatible single-argument call still behaves as a soft clip.
    ADAR1000_AGC legacy;
    legacy.enabled = true;
    legacy.update(true);
    assert(legacy.agc_base_gain == ADAR1000Manager::kDefaultRxVgaGain - legacy.gain_step_down);
}

static void test_hard_overload_cannot_underflow()
{
    ADAR1000_AGC agc;
    agc.enabled = true;
    agc.agc_base_gain = 3;
    agc.update(true, true);                        // step 8 > 3
    assert(agc.agc_base_gain == agc.min_gain);     // clamped, no uint8 wrap
}

// ---------------------------------------------------------------------------
// Recovery time -- the number that must be visible at boot (WP4.3)
// ---------------------------------------------------------------------------
static void test_recovery_time_matches_documented_rate()
{
    ADAR1000_AGC agc;                              // defaults: up 2, holdoff 1
    agc.validate();
    float seconds = agc.recoverySecondsFor(30, 258 /* ms frame */);
    // 30 codes at 2 codes per 1-frame window = 15 frames * 258 ms ~= 3.87 s
    assert(seconds > 3.5f && seconds < 4.2f);

    // Regression guard against the old behaviour: the documented blind window
    // used to be ~31 s (1 code per 4 frames).  If a future edit restores that
    // asymmetry, fail here rather than in the field.
    assert(seconds < 5.0f);

    // A slower configuration is measurably slower.
    agc.gain_step_up = 1;
    agc.holdoff_frames = 4;
    float slow = agc.recoverySecondsFor(30, 258);
    assert(slow > seconds * 5.0f);

    // Degenerate settings must not divide by zero.
    agc.gain_step_up = 0;
    assert(agc.recoverySecondsFor(30, 258) == 0.0f);
}

// ---------------------------------------------------------------------------
// Detector-based per-channel calibration (WP4.4) -- pure helper
// ---------------------------------------------------------------------------
static void test_compute_offsets_equalises_detectors()
{
    uint16_t det[AGC_TOTAL_CHANNELS];
    int8_t off[AGC_TOTAL_CHANNELS];

    // Channel 0 is the reference (strongest), channel 1 is 6 dB weaker -> should
    // get a positive offset, channel 2 is unread (0) -> no offset at all.
    for (int i = 0; i < AGC_TOTAL_CHANNELS; ++i) det[i] = 100;
    det[0] = 200;
    det[1] = 100;
    det[2] = 0;

    ADAR1000_AGC::computeOffsetsFromDetectors(det, off, 12);

    assert(off[0] <= 0);                 // reference is not boosted
    assert(off[1] > 0);                  // weaker channel gets gain
    assert(off[2] == 0);                 // unreadable channel is left alone
    for (int i = 0; i < AGC_TOTAL_CHANNELS; ++i)
        assert(off[i] >= -12 && off[i] <= 12);   // limit honoured
}

static void test_compute_offsets_all_zero_is_noop()
{
    uint16_t det[AGC_TOTAL_CHANNELS] = {0};
    int8_t off[AGC_TOTAL_CHANNELS];
    for (int i = 0; i < AGC_TOTAL_CHANNELS; ++i) off[i] = 7;
    ADAR1000_AGC::computeOffsetsFromDetectors(det, off, 12);
    for (int i = 0; i < AGC_TOTAL_CHANNELS; ++i)
        assert(off[i] == 0);             // a wrong register read cannot poison the table
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    printf("=== ADAR1000_AGC Outer-Loop Unit Tests ===\n");

    RUN_TEST(test_defaults);
    RUN_TEST(test_saturation_reduces_gain);
    RUN_TEST(test_holdoff_prevents_early_gain_up);
    RUN_TEST(test_recovery_after_holdoff);
    RUN_TEST(test_min_gain_clamp);
    RUN_TEST(test_max_gain_clamp);
    RUN_TEST(test_calibration_offsets);
    RUN_TEST(test_disabled_noop);
    RUN_TEST(test_apply_gain_spi);
    RUN_TEST(test_reset_preserves_config);
    RUN_TEST(test_saturation_counter);
    RUN_TEST(test_mixed_sequence);
    RUN_TEST(test_effective_gain_edge_cases);
    // --- Phase 1 additions (feasibility-review WP4.2/4.3/4.4) ---
    RUN_TEST(test_validate_repairs_bad_config);
    RUN_TEST(test_max_gain_clamped_to_vga_range);
    RUN_TEST(test_hard_overload_uses_larger_step);
    RUN_TEST(test_hard_overload_cannot_underflow);
    RUN_TEST(test_recovery_time_matches_documented_rate);
    RUN_TEST(test_compute_offsets_equalises_detectors);
    RUN_TEST(test_compute_offsets_all_zero_is_noop);

    printf("=== Results: %d/%d passed ===\n", tests_passed, tests_total);
    return (tests_passed == tests_total) ? 0 : 1;
}
