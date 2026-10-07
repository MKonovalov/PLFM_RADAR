/**
 * @file    AD9484_SPI.h
 * @brief   The AD9484's serial port, which until now could not be used at all (issue #14).
 *
 * The part's CSB was tied to a supply rail - the datasheet names that as the way to disable the
 * SPI - SCLK/DFS was decided by solder jumper SJ1, and SDIO was not connected. The pins are
 * DRVDD-referenced 1.8 V logic (absolute maximum "SDIO/DCS to DRGND: -0.3 V to DRVDD + 0.2 V",
 * i.e. 2.0 V), so they now reach the MCU through a TXS0104E translator rather than being driven
 * from a 3.3 V bank.
 *
 * The master is the MCU: the ADC, the FPGA and the MCU are all on the main board, the MCU owns
 * bring-up, and the port is slow (tCLK >= 40 ns, 25 MHz maximum), so bit-banging is ample.
 *
 * Frame, from the datasheet's serial timing (and AN-877, which the datasheet points at):
 * a 16-bit instruction - R/W, W1, W0, A12..A0 - followed by 8 bits of data, MSB first. CSB
 * falling starts the frame. Readback turns SDIO around after the instruction, which is why the
 * I/O hook takes a direction control.
 *
 * The registers used here are the ones the AD9484 memory map names:
 *   0x0D TEST_IO      test data on the output pins in place of normal data
 *   0x14 OUTPUT_MODE  data format select: 00 = offset binary (default), 01 = twos complement
 *   0x19/0x1A         user pattern 1, LSB/MSB
 *   0x1B/0x1C         user pattern 2, LSB/MSB
 *   0x2A OVR_CONFIG   OR+/- enable, default on
 */
#ifndef AD9484_SPI_H
#define AD9484_SPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Register addresses, from the AD9484 memory map. */
#define AD9484_REG_SPI_CONFIG   0x00u
#define AD9484_REG_DEVICE_INDEX 0x05u
#define AD9484_REG_TEST_IO      0x0Du
#define AD9484_REG_AIN_CONFIG   0x0Fu
#define AD9484_REG_OUTPUT_MODE  0x14u
#define AD9484_REG_USER_PATT1_LSB 0x19u
#define AD9484_REG_USER_PATT1_MSB 0x1Au
#define AD9484_REG_OVR_CONFIG   0x2Au

/** OUTPUT_MODE data-format field: the FPGA treats the capture as unsigned, i.e. offset binary. */
#define AD9484_FORMAT_OFFSET_BINARY 0x00u
#define AD9484_FORMAT_TWOS_COMPLEMENT 0x01u

/** TEST_IO: write 0 to return the outputs to normal data. */
#define AD9484_TEST_OFF 0x00u
#define AD9484_TEST_USER_PATTERN 0x01u   /* bits[3:0]: user-defined pattern on the outputs */

/** The three lines plus the turnaround control. All optional except csb/sclk/sdio_out. */
typedef struct {
    void (*csb)(bool level);
    void (*sclk)(bool level);
    void (*sdio_out)(bool level);
    void (*sdio_dir)(bool output);       /* true = drive, false = release for readback */
    bool (*sdio_in)(void);
    void (*delay_half_bit)(void);        /* >= 20 ns so tCLK >= 40 ns */
} AD9484_SPI_IO_t;

/** Bind the hooks. Returns false if the essential ones are missing. */
bool AD9484_SPI_Init(const AD9484_SPI_IO_t *io);

/** Write one 8-bit register. */
bool AD9484_SPI_WriteRegister(const AD9484_SPI_IO_t *io, uint8_t addr, uint8_t value);

/** Read one 8-bit register (SDIO turns around after the instruction). */
bool AD9484_SPI_ReadRegister(const AD9484_SPI_IO_t *io, uint8_t addr, uint8_t *value);

/**
 * Put a user-defined pattern on the output pins, or take it off again.
 * With `on` true, `pattern` is loaded into USER_PATT1 (LSB then MSB) and TEST_IO is enabled;
 * with `on` false, TEST_IO is cleared and normal data returns.
 */
bool AD9484_SPI_SetTestPattern(const AD9484_SPI_IO_t *io, bool on, uint16_t pattern);

/** Select the output data format explicitly rather than relying on the SCLK/DFS strap. */
bool AD9484_SPI_SetDataFormat(const AD9484_SPI_IO_t *io, uint8_t format);

/**
 * Read back two registers whose defaults the datasheet states, and report whether both match.
 * This is the bring-up answer to "does the ADC respond": 0x00 CHIP_PORT_CONFIG defaults to 0x18
 * and 0x2A OVR_CONFIG to 0x01, so a mismatch - or a bus that reads all-zero or all-one - is
 * visible as a number rather than as a missing feature.  Nothing is written, so the test is
 * safe to run at any time, including on a part already configured.
 */
bool AD9484_SPI_SelfTest(const AD9484_SPI_IO_t *io, uint8_t *config_read, uint8_t *ovr_read);

#endif /* AD9484_SPI_H */
