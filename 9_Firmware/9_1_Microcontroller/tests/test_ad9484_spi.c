/* The AD9484 serial port (issue #14).
 *
 * The port was unusable - CSB tied to a rail, SCLK/DFS on a jumper, SDIO unconnected - so there
 * was nothing to test before.  This test drives the new master with a line spy and rebuilds the
 * frame from the transitions, which is the only way to check the protocol rather than the code's
 * own bookkeeping: it samples SDIO on every rising SCLK edge while CSB is low.
 */
#include "AD9484_SPI.h"
#include <stdio.h>
#include <string.h>

#define MAX_BITS 64
static char bits[MAX_BITS];      /* the frame, as sampled */
static int nbits;
static char last_frame[MAX_BITS];  /* the most recent frame - the commit, after a write */
static int nlast;
static int cur_frame;              /* 1 = the data frame, 2+ = the commit that follows */
static char driven_bits[MAX_BITS];
static int ndriven;
static int csb_level = 1, sclk_level = 0, sdio_level = 0;
static int csb_frames;           /* how many times CSB fell */
static const char *readback;     /* scripted SDIO bits for a read, MSB first */
static int readback_pos;

static void spy_csb(bool l) { if (!l) { csb_frames++; cur_frame++; if (cur_frame > 1) nlast = 0; } csb_level = l; }
static void spy_sclk(bool l)
{
    if (l && !sclk_level && csb_level == 0) {
        char b = (char)('0' + (sdio_level ? 1 : 0));
        if (cur_frame <= 1) { if (nbits < MAX_BITS) bits[nbits++] = b; }
        else if (nlast < MAX_BITS) last_frame[nlast++] = b;
    }
    sclk_level = l;
}
static void spy_sdio_out(bool l) { sdio_level = l; if (ndriven < MAX_BITS) driven_bits[ndriven++] = (char)('0' + (l ? 1 : 0)); }
static void spy_sdio_dir(bool out) { (void)out; }
static bool spy_sdio_in(void) { return readback && readback_pos < (int)strlen(readback) ? readback[readback_pos++] == '1' : false; }
static void spy_delay(void) { }
static void reset_spy(void) { nbits = ndriven = nlast = 0; cur_frame = 0; csb_frames = 0; readback_pos = 0; sclk_level = 0; sdio_level = 0; csb_level = 1; memset(bits, 0, sizeof bits); memset(driven_bits, 0, sizeof driven_bits); }

static AD9484_SPI_IO_t io = { spy_csb, spy_sclk, spy_sdio_out, spy_sdio_dir, spy_sdio_in, spy_delay };

static int checks, failures;
static void check(int c, const char *what)
{
    checks++;
    if (!c) { failures++; printf("   FAIL: %s\n", what); }
}

int main(void)
{
    printf("=== idle state ===\n");
    check(AD9484_SPI_Init(&io), "init accepts the hooks");
    check(csb_level == 1, "CSB idles high: the port is deselected until we talk");
    check(sclk_level == 0, "SCLK idles low");

    printf("=== a register write: 0x2A = 0x01 (the OVR_CONFIG default) ===\n");
    reset_spy();
    check(AD9484_SPI_WriteRegister(&io, AD9484_REG_OVR_CONFIG, 0x01), "write reports success");
    bits[nbits] = '\0';
    printf("      frame (%d bits): %s\n", nbits, bits);
    check(csb_frames == 2, "two frames: the data, then the commit");
    check(nbits == 24, "16-bit instruction + 8 data bits");
    /* R/W W1 W0 A12..A0 then D7..D0 */
    check(bits[0] == '0', "R/W = 0 for a write");
    check(bits[1] == '0' && bits[2] == '0', "W1 W0 = 00 (1-byte transfer)");
    check(strncmp(bits + 3, "0000000101010", 13) == 0, "address 0x2A in A12..A0, MSB first");
    check(strcmp(bits + 16, "00000001") == 0, "data 0x01, MSB first");

    printf("=== a register read: the turnaround and the returned byte ===\n");
    reset_spy();
    readback = "10110011";          /* what the ADC will shift out */
    uint8_t got = 0;
    check(AD9484_SPI_ReadRegister(&io, AD9484_REG_SPI_CONFIG, &got), "read reports success");
    check(bits[0] == '1', "R/W = 1 for a read");
    check(got == 0xB3, "the returned byte is reassembled MSB first");

    printf("=== the test pattern: pattern loaded before TEST_IO enables it ===\n");
    reset_spy();
    check(AD9484_SPI_SetTestPattern(&io, true, 0x1234), "setting a pattern reports success");
    check(csb_frames == 6, "three registers written, each committed");
    /* the first two frames carry 0x19 then 0x1A, so their address fields differ */
    check(strncmp(bits + 3, "0000000011001", 13) == 0, "USER_PATT1_LSB (0x19) written first");
    check(ndriven > 0, "data went out on SDIO");
    reset_spy();
    check(AD9484_SPI_SetTestPattern(&io, false, 0), "clearing the pattern reports success");
    check(csb_frames == 2, "clearing is a write plus its commit");

    printf("=== the data format can be set explicitly ===\n");
    reset_spy();
    check(AD9484_SPI_SetDataFormat(&io, AD9484_FORMAT_OFFSET_BINARY), "offset binary accepted");
    check(strncmp(bits + 3, "0000000010100", 13) == 0, "OUTPUT_MODE (0x14) is the target");
    check(strcmp(bits + 16, "00000000") == 0, "format field 00 = offset binary");
    check(!AD9484_SPI_SetDataFormat(&io, 0x09), "an out-of-range format is refused");

    printf("=== the bring-up self-test: two registers with documented defaults ===\n");
    reset_spy();
    readback = "00011000" "00000001";     /* 0x00 = 0x18, then 0x2A = 0x01 */
    uint8_t cfg = 0, ovr = 0;
    check(AD9484_SPI_SelfTest(&io, &cfg, &ovr), "both defaults match, so the interface answers");
    check(cfg == 0x18 && ovr == 0x01, "and the values are reported for telemetry");
    reset_spy();
    readback = "00000000" "00000000";     /* a dead bus reads all-zero */
    check(!AD9484_SPI_SelfTest(&io, &cfg, &ovr), "an all-zero bus fails the self-test");
    reset_spy();
    readback = "11111111" "11111111";     /* and a stuck-high bus */
    check(!AD9484_SPI_SelfTest(&io, &cfg, &ovr), "an all-one bus fails the self-test");

    printf("=== every write commits (DEVICE_UPDATE) ===\n");
    reset_spy();
    check(AD9484_SPI_WriteRegister(&io, AD9484_REG_OVR_CONFIG, 0x01), "a write reports success");
    check(csb_frames == 2, "a write is two frames: the data, then the commit");
    /* the commit frame: address 0xFF in the low 8 of the 16-bit instruction, data 0x01 */
    {
        char a[9], v[9];
        for (int i = 0; i < 8; i++) a[i] = (nlast >= 24) ? last_frame[8 + i] : '0';
        for (int i = 0; i < 8; i++) v[i] = (nlast >= 24) ? last_frame[16 + i] : '0';
        a[8] = v[8] = 0;
        check(strcmp(a, "11111111") == 0, "the commit addresses 0xFF");
        check(strcmp(v, "00000001") == 0, "and sets bit 0, the software transfer");
    }

    printf("=== the offset trim ===\n");
    reset_spy();
    check(AD9484_SPI_SetOffsetTrim(&io, -128), "-128 is accepted");
    check(csb_frames == 2, "and commits");
    reset_spy();
    check(AD9484_SPI_SetOffsetTrim(&io, 127), "+127 is accepted");
    reset_spy();
    check(!AD9484_SPI_SetOffsetTrim(&io, 128), "+128 is out of range");
    check(!AD9484_SPI_SetOffsetTrim(&io, -129), "-129 is out of range");

    printf("=== the chip-grade readback ===\n");
    reset_spy();
    readback = "00000101";
    uint8_t grade = 0;
    check(AD9484_SPI_ReadChipGrade(&io, &grade), "the read-only register can be read");
    check(grade == 0x05, "and the value comes back");
    check(!AD9484_SPI_ReadChipGrade(&io, NULL), "a NULL output is refused");

    printf("=== argument checking ===\n");
    check(!AD9484_SPI_Init(NULL), "NULL hooks refused");
    check(!AD9484_SPI_WriteRegister(NULL, 0, 0), "NULL io refused");
    check(!AD9484_SPI_ReadRegister(&io, 0, NULL), "NULL out-pointer refused");
    check(!AD9484_SPI_SelfTest(&io, NULL, NULL), "self-test refuses NULL outputs");
    AD9484_SPI_IO_t incomplete = { spy_csb, NULL, spy_sdio_out, NULL, NULL, NULL };
    check(!AD9484_SPI_Init(&incomplete), "missing SCLK refused");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
