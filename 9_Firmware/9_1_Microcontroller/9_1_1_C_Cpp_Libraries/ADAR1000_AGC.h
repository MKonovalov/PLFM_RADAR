// ADAR1000_AGC.h -- STM32 outer-loop AGC for ADAR1000 RX VGA gain
//
// Adjusts the analog VGA common-mode gain on each ADAR1000 RX channel based on
// the FPGA's saturation flags (DIG_5 / PD13 = "any sample clipped this frame",
// DIG_7 / PD15 = "hard overload", i.e. many samples clipped).  Runs once per
// radar frame (~258 ms) in the main loop, after runRadarPulseSequence().
//
// Architecture:
//   - Inner loop (FPGA, per-sample): rx_gain_control auto-adjusts digital
//     gain_shift based on peak magnitude / saturation.  Range ±42 dB.
//   - Outer loop (THIS MODULE, per-frame): reads the FPGA saturation flags.
//     If saturation detected, reduces agc_base_gain immediately (attack, with a
//     larger step on a hard overload).  If no saturation for holdoff_frames,
//     increases agc_base_gain (decay/recovery).
//
// Per-channel gain formula:
//   VGA[dev][ch] = clamp(agc_base_gain + cal_offset[dev*4+ch], min_gain, max_gain)
//
// The cal_offset array allows per-element calibration to correct inter-channel
// gain imbalance.  Default is all zeros (uniform gain); it can be filled by
// calibrateFromDetectors() (receive-only, from the ADAR1000 on-chip detectors)
// or from a host/GUI command.
//
// Gain-register note: the ADAR1000 RX gain register is 8 bits where the seven
// LSBs drive the VGA (>=31 dB range, <=0.5 dB step) and the MSB selects the
// common-path switched attenuator.  min_gain/max_gain therefore default to
// 0..127 (VGA only); the attenuator bit is deliberately left alone here.

// Recovery-rate design note: the loop is intentionally asymmetric — attack
// 4 codes/frame (8 on a hard overload) against decay 2 codes/frame after a
// 1-frame holdoff.  That keeps the descent faster than the climb (damped, no
// hunting) while removing the old ~31 s blind window: with ~258 ms frames,
// walking back 30 codes now takes ~3.9 s instead of ~31 s.  recoverySecondsFor()
// reports the exact figure at boot so it is never a mystery again.

#ifndef ADAR1000_AGC_H
#define ADAR1000_AGC_H

#include <stdint.h>

// Forward-declare to avoid pulling in the full ADAR1000_Manager header here.
// The .cpp includes the real header.
class ADAR1000Manager;

// Number of ADAR1000 devices
#define AGC_NUM_DEVICES   4
// Number of channels per ADAR1000
#define AGC_NUM_CHANNELS  4
// Total RX channels
#define AGC_TOTAL_CHANNELS (AGC_NUM_DEVICES * AGC_NUM_CHANNELS)

class ADAR1000_AGC {
public:
    // --- Configuration (public for easy field-testing / GUI override) ---

    // Common-mode base gain (raw ADAR1000 VGA register value, 0-127).
    // Default matches ADAR1000Manager::kDefaultRxVgaGain = 30.
    uint8_t agc_base_gain;

    // Per-channel calibration offset (signed, added to agc_base_gain).
    // Index = device*4 + channel.  Default: all 0.
    int8_t cal_offset[AGC_TOTAL_CHANNELS];

    // How much to decrease agc_base_gain per frame when saturated (attack).
    uint8_t gain_step_down;

    // Attack step used when DIG_7 reports a hard overload (many clipped samples
    // in the frame) instead of a marginal clip.
    uint8_t gain_step_down_hard;

    // How much to increase agc_base_gain per frame when recovering (decay).
    uint8_t gain_step_up;

    // Minimum allowed agc_base_gain (floor).
    uint8_t min_gain;

    // Maximum allowed agc_base_gain (ceiling).  127 = VGA-only range.
    uint8_t max_gain;

    // Number of consecutive non-saturated frames required before gain-up.
    uint8_t holdoff_frames;

    // Register read for the per-channel detector calibration.  Flagged because
    // it must be confirmed against the ADAR1000 memory map before use: a wrong
    // address silently yields zero (calibration is then skipped, not corrupt).
    uint32_t detector_reg;

    // Master enable.  When false, update() is a no-op.
    bool enabled;

    // --- Runtime state (read-only for diagnostics) ---

    // Consecutive non-saturated frame counter (resets on saturation).
    uint8_t holdoff_counter;

    // True if the last update() saw saturation.
    bool last_saturated;

    // True if the last update() saw a hard overload (DIG_7).
    bool last_hard_overload;

    // Total saturation events since reset/construction.
    uint32_t saturation_event_count;

    // Total hard-overload events since reset/construction.
    uint32_t hard_overload_count;

    // --- Methods ---

    ADAR1000_AGC();

    // Call once per frame after runRadarPulseSequence().
    // fpga_saturation : DIG_5 == set (any clipped sample this frame)
    // hard_overload   : DIG_7 == set (many clipped samples this frame)
    // Defaulted so existing call sites keep compiling.
    void update(bool fpga_saturation, bool hard_overload = false);

    // Apply the current gain to all 16 RX VGA channels via the Manager.
    void applyGain(ADAR1000Manager &mgr);

    // Apply the gain and verify each write by reading the register back once.
    // Returns the number of channels that failed verification (0 = all good).
    // A failed channel keeps whatever the device holds and is counted, so the
    // caller can raise a structured error instead of flying blind.
    int applyGainVerified(ADAR1000Manager &mgr);

    // Clamp the configuration into a self-consistent state.  Returns true if
    // anything had to change (caller should log it).  Guards the failure modes
    // found in the review: gain_step_down == 0 disables the attack entirely,
    // holdoff_frames == 0 disables the holdoff, and min_gain > max_gain makes
    // effectiveGain() incoherent.
    bool validate();

    // Reset runtime state (holdoff counter, saturation counts) without
    // changing configuration.
    void resetState();

    // Compute the effective gain for a specific channel index (0-15),
    // clamped to [min_gain, max_gain].  Useful for diagnostics.
    uint8_t effectiveGain(uint8_t channel_index) const;

    // Estimated wall-clock time to walk `codes` VGA steps at the configured
    // decay rate (one decay step per holdoff window, i.e. holdoff_frames
    // frames).  Printed at boot so the recovery time is never a mystery.
    float recoverySecondsFor(int codes, uint32_t frame_ms) const;

    // Pure helper: turn 16 detector readings into per-channel gain offsets that
    // equalise them.  Separate so it can be unit-tested without any hardware.
    // `limit` clamps the offset magnitude (e.g. ±12 codes).
    static void computeOffsetsFromDetectors(const uint16_t det[AGC_TOTAL_CHANNELS],
                                           int8_t out[AGC_TOTAL_CHANNELS],
                                           int8_t limit);

    // Receive-only calibration: read the ADAR1000 detector for every channel,
    // derive offsets, store them in cal_offset.  Returns the number of channels
    // actually calibrated (0 if the readback is unusable -> offsets untouched).
    int calibrateFromDetectors(ADAR1000Manager &mgr, int8_t limit = 12);
};

#endif // ADAR1000_AGC_H
