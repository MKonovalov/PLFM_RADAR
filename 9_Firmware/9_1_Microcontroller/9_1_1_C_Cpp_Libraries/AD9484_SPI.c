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

/* One frame on the wire.  This is the shift-register write; it does not commit. */
static bool write_frame(const AD9484_SPI_IO_t *io, uint8_t addr, uint8_t value)
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

bool AD9484_SPI_Commit(const AD9484_SPI_IO_t *io)
{
    /* DEVICE_UPDATE, bit 0: synchronously transfer the shift register to the slave. */
    return write_frame(io, AD9484_REG_DEVICE_UPDATE, 0x01u);
}

bool AD9484_SPI_WriteRegister(const AD9484_SPI_IO_t *io, uint8_t addr, uint8_t value)
{
    if (!write_frame(io, addr, value)) {
        return false;
    }
    return AD9484_SPI_Commit(io);
}

bool AD9484_SPI_SetOffsetTrim(const AD9484_SPI_IO_t *io, int codes)
{
    if (!ready(io) || codes < -128 || codes > 127) {
        return false;
    }
    /* Two's complement in the register: -128 is 0x80, +127 is 0x7F. */
    return AD9484_SPI_WriteRegister(io, AD9484_REG_OFFSET, (uint8_t)(codes & 0xFF));
}

bool AD9484_SPI_ReadChipGrade(const AD9484_SPI_IO_t *io, uint8_t *grade)
{
    if (grade == NULL) {
        return false;
    }
    return AD9484_SPI_ReadRegister(io, AD9484_REG_CHIP_GRADE, grade);
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

/*
 * The datasheet's input voltage range table (FLEX_VREF, 0x18 bits[4:0]).  It is a table and not a
 * formula, and it is not monotonic in the code: 0b11100 is the widest range and 0b00000 the default.
 *
 * The datasheet prints the code 0b01011 twice - once as 1.20 V and once as 1.18 V.  That is an
 * erratum.  The code is listed once here, at the higher value, and the lower entry is not guessed at.
 */
static const struct { uint8_t code; int mv; } AD9484_INPUT_RANGE[] = {
    { 0x1Cu, 1600 }, { 0x1Du, 1580 }, { 0x1Eu, 1550 }, { 0x1Fu, 1520 },
    { 0x00u, 1500 }, { 0x01u, 1470 }, { 0x02u, 1440 }, { 0x03u, 1420 },
    { 0x04u, 1390 }, { 0x05u, 1360 }, { 0x06u, 1340 }, { 0x07u, 1310 },
    { 0x08u, 1280 }, { 0x09u, 1260 }, { 0x0Au, 1230 }, { 0x0Bu, 1200 },
};

int AD9484_SPI_InputRangeMillivolts(uint8_t code)
{
    for (unsigned i = 0; i < sizeof(AD9484_INPUT_RANGE) / sizeof(AD9484_INPUT_RANGE[0]); i++) {
        if (AD9484_INPUT_RANGE[i].code == (code & 0x1Fu)) {
            return AD9484_INPUT_RANGE[i].mv;
        }
    }
    return 0;   /* undocumented - do not write it */
}

bool AD9484_SPI_SetInputRange(const AD9484_SPI_IO_t *io, uint8_t code)
{
    if (!ready(io) || code > 0x1Fu) {
        return false;
    }
    if (AD9484_SPI_InputRangeMillivolts(code) == 0) {
        return false;   /* the datasheet documents no range for this code */
    }
    /* bits[7:6] stay at the internal reference; bits[4:0] are the range. */
    return AD9484_SPI_WriteRegister(io, AD9484_REG_FLEX_VREF,
                                    (uint8_t)(AD9484_VREF_SELECT_INTERNAL | code));
}

bool AD9484_SPI_TestModeIsValid(uint8_t mode)
{
    /* 0000 to 1000 are documented; 1001 to 1111 are unused. */
    return mode <= AD9484_TEST_USER_PATTERN;
}

bool AD9484_SPI_SetTestMode(const AD9484_SPI_IO_t *io, uint8_t mode)
{
    if (!ready(io) || !AD9484_SPI_TestModeIsValid(mode)) {
        return false;
    }
    return AD9484_SPI_WriteRegister(io, AD9484_REG_TEST_IO, (uint8_t)(mode & 0x0Fu));
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

/* Documented defaults, from the AD9484 memory map table. */
#define AD9484_DEFAULT_CHIP_PORT_CONFIG 0x18u
#define AD9484_DEFAULT_OVR_CONFIG 0x01u

bool AD9484_SPI_SelfTest(const AD9484_SPI_IO_t *io, uint8_t *config_read, uint8_t *ovr_read)
{
    if (!ready(io) || config_read == NULL || ovr_read == NULL) {
        return false;
    }
    if (!AD9484_SPI_ReadRegister(io, AD9484_REG_SPI_CONFIG, config_read)) {
        return false;
    }
    if (!AD9484_SPI_ReadRegister(io, AD9484_REG_OVR_CONFIG, ovr_read)) {
        return false;
    }
    return *config_read == AD9484_DEFAULT_CHIP_PORT_CONFIG
        && *ovr_read == AD9484_DEFAULT_OVR_CONFIG;
}
