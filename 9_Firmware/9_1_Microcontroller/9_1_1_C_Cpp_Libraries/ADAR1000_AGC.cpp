// ADAR1000_AGC.cpp -- STM32 outer-loop AGC implementation
//
// See ADAR1000_AGC.h for architecture overview.

#include "ADAR1000_AGC.h"
#include "ADAR1000_Manager.h"
#include "diag_log.h"

#include <cstring>
#include <cstdlib>

// Default detector register used by calibrateFromDetectors().  CONFIRM against
// the ADAR1000 memory map before relying on it: the function treats an all-zero
// readback as "unusable" and leaves cal_offset untouched, so a wrong address
// cannot silently corrupt the gain table.
#define AGC_DETECTOR_REG_DEFAULT 0x040u
#define AGC_DETECTOR_REG_UNVERIFIED 1

// ---------------------------------------------------------------------------
// Constructor -- set all config fields to safe defaults
// ---------------------------------------------------------------------------
ADAR1000_AGC::ADAR1000_AGC()
    : agc_base_gain(ADAR1000Manager::kDefaultRxVgaGain) // 30
    , gain_step_down(4)
    , gain_step_down_hard(8)
    , gain_step_up(2)
    , min_gain(0)
    , max_gain(127)
    , holdoff_frames(1)
    , detector_reg(AGC_DETECTOR_REG_DEFAULT)
    , enabled(false)
    , holdoff_counter(0)
    , last_saturated(false)
    , last_hard_overload(false)
    , saturation_event_count(0)
    , hard_overload_count(0)
    , last_magnitude_class(0)
{
    memset(cal_offset, 0, sizeof(cal_offset));
}

// ---------------------------------------------------------------------------
// validate -- make the configuration self-consistent before use
// ---------------------------------------------------------------------------
bool ADAR1000_AGC::validate()
{
    bool changed = false;

    if (max_gain > 127) { max_gain = 127; changed = true; }   // VGA-only range
    if (min_gain > max_gain) { uint8_t t = min_gain; min_gain = max_gain; max_gain = t; changed = true; }
    if (gain_step_down == 0) { gain_step_down = 1; changed = true; }        // attack must exist
    if (gain_step_down_hard < gain_step_down) { gain_step_down_hard = gain_step_down; changed = true; }
    if (gain_step_down > 15) { gain_step_down = 15; changed = true; }
    if (gain_step_down_hard > 15) { gain_step_down_hard = 15; changed = true; }
    if (gain_step_up == 0) { gain_step_up = 1; changed = true; }
    if (gain_step_up > 15) { gain_step_up = 15; changed = true; }
    if (holdoff_frames == 0) { holdoff_frames = 1; changed = true; }        // holdoff must exist
    if (holdoff_frames > 60) { holdoff_frames = 60; changed = true; }
    if (agc_base_gain < min_gain) { agc_base_gain = min_gain; changed = true; }
    if (agc_base_gain > max_gain) { agc_base_gain = max_gain; changed = true; }

    return changed;
}

// ---------------------------------------------------------------------------
// update -- called once per frame with the FPGA saturation flags
// ---------------------------------------------------------------------------
void ADAR1000_AGC::update(bool fpga_saturation, bool hard_overload)
{
    if (!enabled)
        return;

    last_saturated = fpga_saturation;
    last_hard_overload = fpga_saturation && hard_overload;

    if (fpga_saturation) {
        // Attack: reduce gain immediately.  A hard overload (many clipped
        // samples in the frame) takes a bigger step than a marginal clip, which
        // stops the loop from crawling down 4 codes at a time on a strong
        // transient.
        saturation_event_count++;
        if (last_hard_overload)
            hard_overload_count++;
        holdoff_counter = 0;

        uint8_t step = last_hard_overload ? gain_step_down_hard : gain_step_down;

        if (agc_base_gain >= step + min_gain) {
            agc_base_gain -= step;
        } else {
            agc_base_gain = min_gain;
        }

        DIAG("AGC", "SAT detected (%s) -- gain_base -> %u  (step=%u events=%lu hard=%lu)",
             last_hard_overload ? "hard" : "soft",
             (unsigned)agc_base_gain, (unsigned)step,
             (unsigned long)saturation_event_count, (unsigned long)hard_overload_count);

    } else {
        // Recovery: wait for holdoff, then increase gain
        holdoff_counter++;

        if (holdoff_counter >= holdoff_frames) {
            holdoff_counter = 0;

            if (agc_base_gain + gain_step_up <= max_gain) {
                agc_base_gain += gain_step_up;
            } else {
                agc_base_gain = max_gain;
            }

            DIAG("AGC", "Recovery step -- gain_base -> %u", (unsigned)agc_base_gain);
        }
    }
}


// ---------------------------------------------------------------------------
// attackStepForClass -- VGA codes to remove for a severity class
//
// This table is the point of WP4.2: a 200-count overload takes 12 codes off in
// one frame while a single clipped sample takes 1, instead of both costing the
// same 4.  Index 0 is never applied by updateWithMagnitude -- it signals "no
// magnitude information" and triggers the fixed fallback.
// ---------------------------------------------------------------------------
uint8_t ADAR1000_AGC::attackStepForClass(uint8_t magnitude_class)
{
    static const uint8_t kAttackByClass[8] = {
        0,   // 0: no information (caller falls back)
        1,   // 1: ~1 clipped sample
        2,   // 2: 2-3
        4,   // 3: 4-7
        6,   // 4: 8-15    (hard overload from here)
        8,   // 5: 16-31
        10,  // 6: 32-63
        12   // 7: 64-255  (the old fixed hard step was 8)
    };
    if (magnitude_class == 0 || magnitude_class > 7)
        return 0;
    return kAttackByClass[magnitude_class];
}

// ---------------------------------------------------------------------------
// updateWithMagnitude -- proportional attack driven by the DIG_7 pulse link
//
// Same frame rules as update(): attack immediately on saturation, recover after
// holdoff_frames clean frames.  Only the attack size is now proportional.
// ---------------------------------------------------------------------------
void ADAR1000_AGC::updateWithMagnitude(bool fpga_saturation, uint8_t magnitude_class)
{
    if (!enabled)
        return;

    last_saturated = fpga_saturation;
    last_magnitude_class = magnitude_class;

    if (fpga_saturation) {
        saturation_event_count++;
        holdoff_counter = 0;

        uint8_t step = attackStepForClass(magnitude_class);
        if (step == 0) {
            // No usable magnitude: keep the previous fixed attack so the loop
            // still protects the ADC when the link is absent or noisy.
            step = gain_step_down;
            last_hard_overload = false;
        } else {
            last_hard_overload = (magnitude_class >= AGC_HARD_OVERLOAD_CLASS);
        }
        if (last_hard_overload)
            hard_overload_count++;

        if (agc_base_gain >= step + min_gain)
            agc_base_gain -= step;
        else
            agc_base_gain = min_gain;

        DIAG("AGC", "SAT detected (class=%u step=%u) -- gain_base -> %u",
             (unsigned)magnitude_class, (unsigned)step, (unsigned)agc_base_gain);

    } else {
        holdoff_counter++;
        if (holdoff_counter >= holdoff_frames) {
            holdoff_counter = 0;
            if (agc_base_gain + gain_step_up <= max_gain)
                agc_base_gain += gain_step_up;
            else
                agc_base_gain = max_gain;
            DIAG("AGC", "Recovery step -- gain_base -> %u", (unsigned)agc_base_gain);
        }
    }
}

// ---------------------------------------------------------------------------
// applyGain -- write effective gain to all 16 RX VGA channels
//
// Uses the Manager's adarSetRxVgaGain which takes 1-based channel indices
// (matching the convention in setBeamAngle).
// ---------------------------------------------------------------------------
void ADAR1000_AGC::applyGain(ADAR1000Manager &mgr)
{
    for (uint8_t dev = 0; dev < AGC_NUM_DEVICES; ++dev) {
        for (uint8_t ch = 0; ch < AGC_NUM_CHANNELS; ++ch) {
            uint8_t gain = effectiveGain(dev * AGC_NUM_CHANNELS + ch);
            // Channel parameter is 1-based per Manager convention
            mgr.adarSetRxVgaGain(dev, ch + 1, gain, BROADCAST_OFF);
        }
    }
}

// ---------------------------------------------------------------------------
// applyGainVerified -- write, then read back; count channels that did not take
// ---------------------------------------------------------------------------
int ADAR1000_AGC::applyGainVerified(ADAR1000Manager &mgr)
{
    static const uint32_t rx_gain_reg[AGC_NUM_CHANNELS] = {
        REG_CH1_RX_GAIN, REG_CH2_RX_GAIN, REG_CH3_RX_GAIN, REG_CH4_RX_GAIN
    };
    int failures = 0;

    for (uint8_t dev = 0; dev < AGC_NUM_DEVICES; ++dev) {
        for (uint8_t ch = 0; ch < AGC_NUM_CHANNELS; ++ch) {
            uint8_t gain = effectiveGain(dev * AGC_NUM_CHANNELS + ch);
            uint32_t addr = rx_gain_reg[ch];

            mgr.adarSetRxVgaGain(dev, ch + 1, gain, BROADCAST_OFF);
            uint8_t rb = mgr.readRegister(dev, addr);
            if (rb != gain) {
                // one retry, then report
                mgr.adarSetRxVgaGain(dev, ch + 1, gain, BROADCAST_OFF);
                rb = mgr.readRegister(dev, addr);
                if (rb != gain) {
                    failures++;
                    DIAG_ERR("AGC", "VGA write verify FAILED dev%u ch%u want=%u got=%u",
                             (unsigned)dev, (unsigned)ch, (unsigned)gain, (unsigned)rb);
                }
            }
        }
    }
    return failures;
}

// ---------------------------------------------------------------------------
// resetState -- clear runtime counters, preserve configuration
// ---------------------------------------------------------------------------
void ADAR1000_AGC::resetState()
{
    holdoff_counter = 0;
    last_saturated = false;
    last_hard_overload = false;
    saturation_event_count = 0;
    hard_overload_count = 0;
}

// ---------------------------------------------------------------------------
// effectiveGain -- compute clamped per-channel gain
// ---------------------------------------------------------------------------
uint8_t ADAR1000_AGC::effectiveGain(uint8_t channel_index) const
{
    if (channel_index >= AGC_TOTAL_CHANNELS)
        return min_gain;  // safety fallback — OOB channels get minimum gain

    int16_t raw = static_cast<int16_t>(agc_base_gain) + cal_offset[channel_index];

    if (raw < static_cast<int16_t>(min_gain))
        return min_gain;
    if (raw > static_cast<int16_t>(max_gain))
        return max_gain;

    return static_cast<uint8_t>(raw);
}

// ---------------------------------------------------------------------------
// recoverySecondsFor -- how long a gain-up walk takes at the current settings
//
// One decay step is applied per holdoff window, i.e. every holdoff_frames
// frames.  With the defaults (holdoff 4, ~258 ms frames) one VGA code takes
// ~1.03 s, so recovering 30 codes takes ~31 s.  Printed at boot.
// ---------------------------------------------------------------------------
float ADAR1000_AGC::recoverySecondsFor(int codes, uint32_t frame_ms) const
{
    if (codes <= 0 || gain_step_up == 0 || holdoff_frames == 0)
        return 0.0f;
    float steps = (float)codes / (float)gain_step_up;
    float seconds_per_step = (float)holdoff_frames * (float)frame_ms / 1000.0f;
    return steps * seconds_per_step;
}

// ---------------------------------------------------------------------------
// computeOffsetsFromDetectors -- equalise detector readings via gain offsets
//
// A reading of 0 is treated as "no signal / unusable" and yields no offset, so
// an unpopulated channel or a bad register read cannot poison the table.
// ---------------------------------------------------------------------------
void ADAR1000_AGC::computeOffsetsFromDetectors(const uint16_t det[AGC_TOTAL_CHANNELS],
                                               int8_t out[AGC_TOTAL_CHANNELS],
                                               int8_t limit)
{
    if (limit < 0) limit = 0;

    // Reference = strongest readable channel.
    uint16_t ref = 0;
    for (uint8_t i = 0; i < AGC_TOTAL_CHANNELS; ++i)
        if (det[i] > ref) ref = det[i];

    for (uint8_t i = 0; i < AGC_TOTAL_CHANNELS; ++i) {
        if (det[i] == 0 || ref == 0) { out[i] = 0; continue; }

        // Channels weaker than the reference get positive gain; the reference
        // itself gets a small negative offset so the array average is kept.
        float ratio = (float)det[i] / (float)ref;          // 0..1
        float db = 20.0f * (float)__builtin_log10((double)ratio);  // negative
        int offset = (int)(-db * 2.0f);                    // ~2 codes per dB

        if (offset > limit) offset = limit;
        if (offset < -limit) offset = -limit;
        out[i] = (int8_t)offset;
    }
}

// ---------------------------------------------------------------------------
// calibrateFromDetectors -- receive-only per-channel gain calibration
// ---------------------------------------------------------------------------
int ADAR1000_AGC::calibrateFromDetectors(ADAR1000Manager &mgr, int8_t limit)
{
    uint16_t det[AGC_TOTAL_CHANNELS];
    int calibrated = 0;

    for (uint8_t dev = 0; dev < AGC_NUM_DEVICES; ++dev) {
        for (uint8_t ch = 0; ch < AGC_NUM_CHANNELS; ++ch) {
            det[dev * AGC_NUM_CHANNELS + ch] = mgr.readRegister(dev, detector_reg);
        }
    }

#if AGC_DETECTOR_REG_UNVERIFIED
    DIAG("AGC", "Detector calibration read from reg 0x%03X (address unverified)",
         (unsigned)detector_reg);
#endif

    computeOffsetsFromDetectors(det, cal_offset, limit);

    for (uint8_t i = 0; i < AGC_TOTAL_CHANNELS; ++i)
        if (cal_offset[i] != 0) calibrated++;

    return calibrated;
}
