/**
 * @file    AD9484_SPI.c
 * @brief   Bit-banged master for the AD9484's serial port.  See AD9484_SPI.h.
 *
 * Frame: CSB low starts it, then a 16-bit instruction (R/W, W1, W0, A12..A0) and 8 data bits,
 * all MSB first, sampled on the rising edge of SCLK with the data set up on the falling edge.
 * That gives tDS/tDH well inside the datasheet's 5 ns / 2 ns, because a half bit is far longer
 * than either.
 */
#include "AD9484_SPI.h"

#define INSTRUCTION_BITS 16u
#define DATA_BITS 8u

static bool ready(const AD9484_SPI_IO_t *io)
{
    return io != NULL && io->csb != NULL && io->sclk != NULL && io->sdio_out != NULL;
}

static void half_bit(const AD9484_SPI_IO_t *io)
{
    if (io->delay_half_bit != NULL) {
        io->delay_half_bit();
    }
}

/** Shift out one bit, MSB-first convention handled by the caller. */
static void clock_out_bit(const AD9484_SPI_IO_t *io, bool bit)
{
    io->sdio_out(bit);
    half_bit(io);
    io->sclk(true);
    half_bit(io);
    io->sclk(false);
}

static bool clock_in_bit(const AD9484_SPI_IO_t *io)
{
    half_bit(io);
    io->sclk(true);
    half_bit(io);
    bool bit = (io->sdio_in != NULL) ? io->sdio_in() : false;
    io->sclk(false);
    return bit;
}

bool AD9484_SPI_Init(const AD9484_SPI_IO_t *io)
{
    if (!ready(io)) {
        return false;
    }
    /* Idle: CSB high (the port deselected, which is also the power-up state), clock low. */
    io->csb(true);
    io->sclk(false);
    if (io->sdio_dir != NULL) {
        io->sdio_dir(true);
    }
    io->sdio_out(false);
    return true;
}

static void send_instruction(const AD9484_SPI_IO_t *io, bool read, uint8_t addr)
{
    /* R/W, then W1 W0 (both 0: a 1-byte transfer), then A12..A0. */
    clock_out_bit(io, read);
    clock_out_bit(io, false);
    clock_out_bit(io, false);
    for (int bit = 12; bit >= 0; bit--) {
        clock_out_bit(io, ((addr >> bit) & 1u) != 0u);
    }
}

bool AD9484_SPI_WriteRegister(const AD9484_SPI_IO_t *io, uint8_t addr, uint8_t value)
{
    if (!ready(io)) {
        return false;
    }
    io->csb(false);
    half_bit(io);
    if (io->sdio_dir != NULL) {
        io->sdio_dir(true);
    }
    send_instruction(io, false, addr);
    for (int bit = DATA_BITS - 1; bit >= 0; bit--) {
        clock_out_bit(io, ((value >> bit) & 1u) != 0u);
    }
    half_bit(io);
    io->csb(true);          /* the frame ends here */
    return true;
}

bool AD9484_SPI_ReadRegister(const AD9484_SPI_IO_t *io, uint8_t addr, uint8_t *value)
{
    if (!ready(io) || value == NULL || io->sdio_in == NULL) {
        return false;
    }
    io->csb(false);
    half_bit(io);
    if (io->sdio_dir != NULL) {
        io->sdio_dir(true);
    }
    send_instruction(io, true, addr);
    if (io->sdio_dir != NULL) {
        io->sdio_dir(false);        /* SDIO turns around for the data phase */
    }
    uint8_t got = 0;
    for (unsigned i = 0; i < DATA_BITS; i++) {
        got = (uint8_t)((got << 1) | (clock_in_bit(io) ? 1u : 0u));
    }
    half_bit(io);
    io->csb(true);
    if (io->sdio_dir != NULL) {
        io->sdio_dir(true);
    }
    *value = got;
    return true;
}

bool AD9484_SPI_SetTestPattern(const AD9484_SPI_IO_t *io, bool on, uint16_t pattern)
{
    if (!ready(io)) {
        return false;
    }
    if (!on) {
        return AD9484_SPI_WriteRegister(io, AD9484_REG_TEST_IO, AD9484_TEST_OFF);
    }
    /* USER_PATT1 holds the value the outputs repeat: LSB register first, then MSB. */
    if (!AD9484_SPI_WriteRegister(io, AD9484_REG_USER_PATT1_LSB, (uint8_t)(pattern & 0xFFu))) {
        return false;
    }
    if (!AD9484_SPI_WriteRegister(io, AD9484_REG_USER_PATT1_MSB, (uint8_t)(pattern >> 8))) {
        return false;
    }
    return AD9484_SPI_WriteRegister(io, AD9484_REG_TEST_IO, AD9484_TEST_USER_PATTERN);
}

bool AD9484_SPI_SetDataFormat(const AD9484_SPI_IO_t *io, uint8_t format)
{
    if (!ready(io) || format > AD9484_FORMAT_TWOS_COMPLEMENT) {
        return false;
    }
    /* OUTPUT_MODE bits[1:0] select the format; everything else keeps its default. */
    return AD9484_SPI_WriteRegister(io, AD9484_REG_OUTPUT_MODE, (uint8_t)(format & 0x03u));
}
