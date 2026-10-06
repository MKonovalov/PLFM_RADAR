/* Rail-good monitoring: the latch is the point.
 *
 * A rail that dips and recovers has still told you something. This test drives the monitor
 * with scripted pin levels and asserts the three behaviours that matter: a fault latches, a
 * cleared latch stays cleared until the next fault, and a missing source counts as a fault
 * rather than as good.
 */
#include "RAIL_FAULT.h"
#include <stdio.h>

static bool rails[3];
static bool ok0(void) { return rails[0]; }
static bool ok1(void) { return rails[1]; }
static bool ok2(void) { return rails[2]; }
static bool ok_never_configured(void) { return rails[2]; }

static int checks, failures;
static void check(bool c, const char *what)
{
    checks++;
    if (!c) { failures++; printf("   FAIL: %s\n", what); }
}

int main(void)
{
    RailFault_Source_t srcs[3] = { { ok0 }, { ok1 }, { ok2 } };
    RailFault_Monitor_t mon;

    printf("=== binding ===\n");
    check(RailFault_Init(&mon, srcs, 3), "init accepts three sources");
    check(!RailFault_Init(&mon, srcs, 0), "init refuses zero sources");
    check(!RailFault_Init(NULL, srcs, 3), "init refuses a NULL monitor");
    check(!RailFault_Init(&mon, srcs, 33), "init refuses more than 32 sources");
    check(RailFault_Init(&mon, srcs, 3), "re-init succeeds");

    printf("=== all good: no fault, nothing latched ===\n");
    rails[0] = rails[1] = rails[2] = true;
    check(!RailFault_Sample(&mon), "sample reports no fault");
    check(!RailFault_HasLatched(&mon), "and nothing is latched");

    printf("=== a dip on one rail latches that rail only ===\n");
    rails[1] = false;
    check(RailFault_Sample(&mon), "sample reports a fault");
    check(mon.current == (1u << 1), "the current bitmap names rail 1");
    check(RailFault_HasLatched(&mon), "and it is latched");

    printf("=== recovery does not clear the latch ===\n");
    rails[1] = true;
    check(!RailFault_Sample(&mon), "sample now reports no current fault");
    check(mon.current == 0, "the current bitmap is clear");
    check(RailFault_HasLatched(&mon), "but the latch survives recovery");
    check(mon.latched == (1u << 1), "and still names rail 1");

    printf("=== clearing is deliberate ===\n");
    RailFault_ClearLatch(&mon);
    check(!RailFault_HasLatched(&mon), "the latch is clear after an explicit clear");
    rails[2] = false;
    RailFault_Sample(&mon);
    check(mon.latched == (1u << 2), "the next fault latches on its own rail");

    printf("=== a missing source counts as a fault, not as good ===\n");
    RailFault_Source_t bad[2] = { { ok0 }, { NULL } };
    RailFault_Monitor_t mon2;
    rails[0] = true;
    check(RailFault_Init(&mon2, bad, 2), "init accepts a partially wired source list");
    check(RailFault_Sample(&mon2), "the unwired source reads as a fault");
    check(mon2.latched == (1u << 1), "and latches on its own bit");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
