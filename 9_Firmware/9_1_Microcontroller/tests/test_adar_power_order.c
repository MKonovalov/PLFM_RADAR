/*******************************************************************************
 * test_adar_power_order.c
 *
 * Issue #20: the ADAR1000's supply order.
 *
 * The datasheet's pin table (AVDD3, pins M10/M11/N11) says:
 *
 *   "3.3 V Voltage Power Supply Inputs. It is recommended to power-up these pins
 *    before or at the same time as the AVDD1(-5V) supply."
 *
 * On this board the -5 V rails (-5V0_ADAR12/34) are produced by LM2662 inverters
 * fed from +5V0_ADAR, so enabling +5V0_ADAR is what brings the negative up. The
 * firmware therefore has to assert EN_P_3V3_ADAR12/34 *before* (or with)
 * EN_P_5V0_ADAR, and there has to be a settle delay before the negative is loaded.
 *
 * The order in main.cpp is currently correct. This test exists so that a later edit
 * cannot silently swap it: it reads the source and asserts the offsets are ascending,
 * rather than re-implementing the sequence and asserting on its own copy.
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path, long *out_size)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }
    long nread = (long)fread(buf, 1, (size_t)size, f);
    buf[nread] = '\0';
    fclose(f);
    if (out_size) *out_size = nread;
    return buf;
}

static int checks, failures;
static void check(int cond, const char *what)
{
    checks++;
    if (!cond) { failures++; printf("   FAIL: %s\n", what); }
}

int main(void)
{
    long size = 0;
    char *src = read_file("../9_1_3_C_Cpp_Code/main.cpp", &size);
    if (!src) {
        printf("could not read main.cpp (run from the tests directory)\n");
        return 2;
    }

    printf("=== the ADAR1000 power-up block exists ===\n");
    char *section = strstr(src, "ADAR1000 POWER SEQUENCING");
    check(section != NULL, "the ADAR1000 sequencing section is present");

    printf("=== the required order: 3.3 V before the 5.0 V that creates -5 V ===\n");
    char *mixer_off = section ? strstr(section, "Disabling TX mixers (GPIOD pin 11 LOW)") : NULL;
    char *en_3v3 = section ? strstr(section, "Enabling 3.3V ADAR12 + ADAR34 rails") : NULL;
    char *en_5v0 = section ? strstr(section, "Enabling 5.0V ADAR rail") : NULL;
    check(mixer_off != NULL, "the mixers are disabled before the rails come up");
    check(en_3v3 != NULL, "the 3.3 V ADAR rails are enabled");
    check(en_5v0 != NULL, "the 5.0 V ADAR rail is enabled");
    if (mixer_off && en_3v3 && en_5v0) {
        check(mixer_off < en_3v3, "TX RF is off before the rails are energised");
        check(en_3v3 < en_5v0,
              "AVDD3 (+3.3 V) is asserted before AVDD1 (-5 V) is created by the pump");
        /* the settle delay must sit between the two enables, not after both */
        char *settle = strstr(en_3v3, "HAL_Delay(");
        check(settle != NULL && settle < en_5v0,
              "a settle delay separates the 3.3 V rails from the 5.0 V rail");
    }

    printf("=== the requirement is written down next to the code ===\n");
    check(strstr(src, "AVDD1") != NULL || strstr(src, "AVDD3") != NULL,
          "the pin names are named in the source so the reason survives");

    printf("\n%d checks, %d failures\n", checks, failures);
    free(src);
    return failures ? 1 : 0;
}
