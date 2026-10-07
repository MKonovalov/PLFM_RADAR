/**
 * @file    AD9523_VERIFY.h
 * @brief   Read the AD9523's outputs back, so "the unused ones are off" is a measurement (issue #17).
 *
 * The driver already handles the disable: any channel not in the active mask is written with
 * DRIVER_MODE(TRISTATE) | PWR_DOWN_EN, and a channel whose platform data sets output_dis gets the
 * same bit.  That means OUT2/OUT3 - the four nets with nothing but the driver on them - are off,
 * and an output that is tristated and powered down cannot radiate into a dangling stub.
 *
 * What was missing was the confirmation the issue's acceptance criteria ask for: a readback.  This
 * reads the fourteen channel registers back and reports which are powered down, so the claim rests
 * on the part's own answer rather than on the firmware's intent.
 *
 * AD9523_CHANNEL_CLOCK_DIST(ch) is 0x192 + 3*ch and AD9523_CLK_DIST_PWR_DOWN_EN is bit 5.
 */
#ifndef AD9523_VERIFY_H
#define AD9523_VERIFY_H

#include <stdint.h>
#include "ad9523.h"

/** The number of output channels the readback walks. */
#define AD9523_VERIFY_CHANNELS AD9523_NUM_CHAN

/**
 * Read every channel's clock-distribution register and report which outputs are powered down.
 *
 * @param dev             the device, already initialised
 * @param powered_down    out: bit n set means channel n reads back powered down
 * @param readable        out: bit n set means channel n's register could be read at all
 *
 * @return 0 on success, or the driver's negative error code if no register could be read.
 */
int32_t AD9523_VerifyOutputs(struct ad9523_dev *dev, uint32_t *powered_down, uint32_t *readable);

/**
 * Decode one clock-distribution register value.
 * Split out so it can be tested without a bus: this is the whole of the logic.
 */
static inline int AD9523_OutputIsPoweredDown(uint32_t reg_value)
{
    return (reg_value & AD9523_CLK_DIST_PWR_DOWN_EN) != 0u;
}

#endif /* AD9523_VERIFY_H */
