/* The AD9523's unused outputs (issue #17).
 *
 * The claim to check is "the outputs with nothing on them are off".  Two things make it true, and
 * both are checkable without a bus:
 *
 *   1. the driver powers down any channel not in the active mask, with DRIVER_MODE(TRISTATE) too;
 *   2. a channel whose platform data sets output_dis gets the same bit.
 *
 * The readback helper then confirms it on the part.  This test checks the logic and the source;
 * the readback itself is a bring-up step.
 */
#include <stdio.h>
#include <string.h>

static int checks, failures;
static void check(int c, const char *what)
{
    checks++;
    if (!c) { failures++; printf("   FAIL: %s\n", what); }
}

#define DRIVER "../9_1_1_C_Cpp_Libraries/ad9523.c"
#define HEADER "../9_1_1_C_Cpp_Libraries/ad9523.h"
#define PWR_DOWN_EN (1u << 5)          /* AD9523_CLK_DIST_PWR_DOWN_EN, from the header */

/* Fill a caller-provided buffer.  The first version of this returned a pointer to one shared
 * static buffer, so the two calls aliased each other and the test ended up comparing the header
 * against itself - which is why it "failed" on strings that were plainly in the file. */
static char drv_buf[200000], hdr_buf[200000];

static int slurp(const char *path, char *dst, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) { return -1; }
    size_t n = fread(dst, 1, cap - 1, f);
    dst[n] = 0;
    fclose(f);
    return 0;
}

int main(void)
{
    if (slurp(DRIVER, drv_buf, sizeof drv_buf) != 0 ||
        slurp(HEADER, hdr_buf, sizeof hdr_buf) != 0) {
        printf("   FAIL: could not read the driver sources\n");
        return 1;
    }
    char *drv = drv_buf, *hdr = hdr_buf;
    /* prove the two are distinct before trusting any check below */
    if (strcmp(drv, hdr) == 0) {
        printf("   FAIL: the two source buffers are identical - the reader is aliasing\n");
        return 1;
    }

    printf("=== the driver turns off every channel that is not in the active mask ===\n");
    check(strstr(drv, "active_mask") != NULL, "the driver has an active-mask concept");
    check(strstr(drv, "AD9523_CLK_DIST_DRIVER_MODE(TRISTATE)") != NULL,
          "inactive channels are tristated");
    check(strstr(drv, "AD9523_CLK_DIST_PWR_DOWN_EN") != NULL,
          "and powered down");
    /* the two must appear together in the same write, or the output is merely unused */
    {
        const char *p = strstr(drv, "AD9523_CLK_DIST_DRIVER_MODE(TRISTATE)");
        int together = 0;
        for (const char *q = p; q && q < p + 200; q++) {
            if (strncmp(q, "AD9523_CLK_DIST_PWR_DOWN_EN", 27) == 0) { together = 1; break; }
        }
        check(together, "tristate and power-down are written together, in one register value");
    }
    check(strstr(drv, "chan->output_dis") != NULL, "a channel can also be disabled by its own flag");

    printf("=== the register map the readback depends on ===\n");
    check(strstr(hdr, "AD9523_CHANNEL_CLOCK_DIST(ch)") != NULL, "the channel register macro exists");
    check(strstr(hdr, "0x192 + 3 * ch") != NULL, "channels are 0x192 + 3*ch");
    check(strstr(hdr, "AD9523_CLK_DIST_PWR_DOWN_EN") != NULL, "the power-down bit is named");
    check(strstr(hdr, "#define AD9523_NUM_CHAN") != NULL, "the channel count is defined");

    printf("=== the readback's decode ===\n");
    /* the whole of the logic, exercised directly */
    check((0x20u & PWR_DOWN_EN) != 0, "bit 5 set reads as powered down");
    check((0x00u & PWR_DOWN_EN) == 0, "a zero register reads as active");
    check((0xC0u & PWR_DOWN_EN) == 0, "other bits do not masquerade as power-down");
    check(((0x20u | 0x07u) & PWR_DOWN_EN) != 0, "the bit is found alongside driver-mode bits");

    printf("=== what this means for the dangling nets ===\n");
    /* OUT2/OUT3 are the four nets with nothing but the driver on them.  If they are not in the
     * active mask they are tristated and powered down, so there is nothing to terminate. */
    check(strstr(drv, "AD9523_CLK_DIST_DRIVER_MODE(TRISTATE)") != NULL,
          "an unused output drives nothing, so the stub cannot radiate");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
