/**
 * @file    AD9523_VERIFY.c
 * @brief   Read the AD9523's outputs back.  See AD9523_VERIFY.h.
 */
#include "AD9523_VERIFY.h"

int32_t AD9523_VerifyOutputs(struct ad9523_dev *dev, uint32_t *powered_down, uint32_t *readable)
{
    if (dev == NULL || powered_down == NULL || readable == NULL) {
        return -1;
    }
    uint32_t pd = 0u, rd = 0u;
    int32_t last_error = 0;

    for (uint32_t ch = 0; ch < AD9523_VERIFY_CHANNELS; ch++) {
        uint32_t value = 0u;
        int32_t ret = ad9523_spi_read(dev, AD9523_CHANNEL_CLOCK_DIST(ch), &value);
        if (ret < 0) {
            last_error = ret;          /* one silent channel does not invalidate the others */
            continue;
        }
        rd |= (1u << ch);
        if (AD9523_OutputIsPoweredDown(value)) {
            pd |= (1u << ch);
        }
    }
    *powered_down = pd;
    *readable = rd;
    return rd ? 0 : last_error;
}
