/* The MCU's side of the strobe acknowledgement (issue #13).
 *
 * The property that matters: a request that is never acknowledged must be *scored* before the
 * next request goes out, because the FPGA clears the line as soon as it sees the new request.
 * Checking later would always read low and the drop would be invisible.
 */
#include "STROBE_ACK.h"
#include <stdio.h>

static int checks, failures;
static void check(int c, const char *what)
{
    checks++;
    if (!c) { failures++; printf("   FAIL: %s\n", what); }
}

int main(void)
{
    StrobeAck_State_t st;

    printf("=== a request that is acknowledged ===\n");
    StrobeAck_Init(&st);
    StrobeAck_BeforeSend(&st);            /* send #1 */
    StrobeAck_Sample(&st, false);         /* nothing yet */
    StrobeAck_Sample(&st, true);          /* the FPGA took it */
    StrobeAck_BeforeSend(&st);            /* send #2 - #1 was fine */
    check(st.sent == 2, "two requests sent");
    check(st.missed == 0, "neither was missed");
    check(st.acknowledged == 1, "the first was acknowledged");

    printf("=== a request that is dropped ===\n");
    StrobeAck_Init(&st);
    StrobeAck_BeforeSend(&st);            /* send #1 */
    StrobeAck_Sample(&st, false);
    StrobeAck_BeforeSend(&st);            /* send #2 - #1 was never taken */
    check(st.missed == 1, "the dropped request is scored");
    check(StrobeAck_HasMissed(&st), "and reported");

    printf("=== the line is sampled repeatedly without double-counting ===\n");
    StrobeAck_Init(&st);
    StrobeAck_BeforeSend(&st);
    StrobeAck_Sample(&st, true);
    StrobeAck_Sample(&st, true);
    StrobeAck_Sample(&st, true);
    check(st.acknowledged == 1, "a held line counts once");

    printf("=== recovery: a miss does not poison the count ===\n");
    StrobeAck_Init(&st);
    StrobeAck_BeforeSend(&st);
    StrobeAck_BeforeSend(&st);            /* #1 missed */
    StrobeAck_Sample(&st, true);
    StrobeAck_BeforeSend(&st);            /* #2 was fine */
    check(st.missed == 1, "exactly one miss");
    check(st.sent == 3, "three sent");

    printf("=== the first request cannot be a miss ===\n");
    StrobeAck_Init(&st);
    StrobeAck_BeforeSend(&st);
    check(st.missed == 0, "nothing to score before the first send");

    printf("=== the path self-test: is the wire alive? ===\n");
    StrobeAck_SelfTest_t stt;
    StrobeAck_SelfTest_Begin(&stt, 50);
    check(StrobeAck_SelfTest_Poll(&stt, 10, false) == STROBE_SELFTEST_WAITING,
          "waiting while the acknowledgement is low");
    check(StrobeAck_SelfTest_Poll(&stt, 10, true) == STROBE_SELFTEST_PASSED,
          "passes as soon as the acknowledgement rises");
    check(stt.passed && !stt.running, "and stops running once it has passed");

    StrobeAck_SelfTest_Begin(&stt, 30);
    check(StrobeAck_SelfTest_Poll(&stt, 10, false) == STROBE_SELFTEST_WAITING, "still waiting");
    check(StrobeAck_SelfTest_Poll(&stt, 10, false) == STROBE_SELFTEST_WAITING, "still waiting");
    check(StrobeAck_SelfTest_Poll(&stt, 10, false) == STROBE_SELFTEST_TIMEOUT,
          "times out when the acknowledgement never comes");
    check(!stt.passed, "and does not report a pass");

    StrobeAck_SelfTest_Begin(&stt, 20);
    check(StrobeAck_SelfTest_Poll(&stt, 20, true) == STROBE_SELFTEST_PASSED,
          "a pass on the very last poll still counts");

    printf("=== null-safety ===\n");
    StrobeAck_Init(NULL);
    StrobeAck_BeforeSend(NULL);
    StrobeAck_Sample(NULL, true);
    StrobeAck_SelfTest_Begin(NULL, 10);
    check(StrobeAck_SelfTest_Poll(NULL, 10, true) == STROBE_SELFTEST_TIMEOUT, "NULL poll is a timeout");
    check(!StrobeAck_HasMissed(NULL), "NULL is not a miss");
    check(1, "no crash");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
