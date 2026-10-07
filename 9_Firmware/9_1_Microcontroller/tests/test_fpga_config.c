/* The FPGA configuration status (issue #13).
 *
 * The acceptance criterion is "force a failed configuration: the MCU must report it".  The line is
 * a level, so the whole behaviour is: does it go high, and if it does not, does the firmware say so.
 */
#include "FPGA_CONFIG.h"
#include <stdio.h>

static int checks, failures;
static void check(int c, const char *what)
{
    checks++;
    if (!c) { failures++; printf("   FAIL: %s\n", what); }
}

int main(void)
{
    FpgaConfig_t cfg;

    printf("=== a good configuration ===\n");
    FpgaConfig_Init(&cfg);
    check(cfg.state == FPGA_CFG_UNKNOWN, "starts unknown, not pretending");
    check(!FpgaConfig_IsConfigured(&cfg), "and not configured");
    FpgaConfig_Sample(&cfg, false, 1000);
    check(cfg.state == FPGA_CFG_IN_PROGRESS, "a low line is 'configuring' at first, not a failure");
    check(!FpgaConfig_HasFailed(&cfg), "and is not reported as failed inside the timeout");
    FpgaConfig_Sample(&cfg, true, 1100);
    check(cfg.state == FPGA_CFG_DONE, "the line going high is DONE");
    check(FpgaConfig_IsConfigured(&cfg), "and the FPGA counts as configured");
    check(!FpgaConfig_HasFailed(&cfg), "with no failure reported");

    printf("=== a transient low after configuration is not a regression ===\n");
    FpgaConfig_Sample(&cfg, false, 5000);
    check(FpgaConfig_IsConfigured(&cfg), "a configured FPGA stays configured");
    check(cfg.state == FPGA_CFG_DONE, "and the state is unchanged");
    check(!FpgaConfig_HasFailed(&cfg), "so no failure is raised");

    printf("=== a failed configuration is reported (the acceptance case) ===\n");
    FpgaConfig_Init(&cfg);
    FpgaConfig_Sample(&cfg, false, 0);              /* INIT_B held low from the start */
    check(cfg.state == FPGA_CFG_IN_PROGRESS, "in progress while the timeout runs");
    FpgaConfig_Sample(&cfg, false, FPGA_CFG_TIMEOUT_MS - 1);
    check(cfg.state == FPGA_CFG_IN_PROGRESS, "still in progress just before the timeout");
    FpgaConfig_Sample(&cfg, false, FPGA_CFG_TIMEOUT_MS);
    check(cfg.state == FPGA_CFG_FAILED, "at the timeout the configuration has failed");
    check(FpgaConfig_HasFailed(&cfg), "and the MCU can report it");
    check(!FpgaConfig_IsConfigured(&cfg), "and it is not configured");

    printf("=== it stays failed, and recovers if the FPGA then configures ===\n");
    FpgaConfig_Sample(&cfg, false, 100000);
    check(FpgaConfig_HasFailed(&cfg), "still failed long afterwards");
    FpgaConfig_Sample(&cfg, true, 100100);
    check(FpgaConfig_IsConfigured(&cfg) && !FpgaConfig_HasFailed(&cfg),
          "a late configuration clears the failure");

    printf("=== the names are for telemetry ===\n");
    check(FpgaConfig_StateName(FPGA_CFG_DONE)[0] == 'd', "DONE names itself");
    check(FpgaConfig_StateName(FPGA_CFG_FAILED)[0] == 'F', "FAILED is loud, not lowercase");
    check(FpgaConfig_StateName(FPGA_CFG_UNKNOWN)[0] == 'u', "unknown says so");

    printf("=== null-safety ===\n");
    FpgaConfig_Init(NULL);
    FpgaConfig_Sample(NULL, true, 0);
    check(!FpgaConfig_IsConfigured(NULL) && !FpgaConfig_HasFailed(NULL), "NULL is safe and not 'configured'");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
