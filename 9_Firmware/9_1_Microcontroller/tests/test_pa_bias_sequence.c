/* The PA bias sequence: the order is the safety property, so the order is what is tested.
 *
 * With 16 depletion-mode GaN HEMTs, applying drain voltage while the gate sits above
 * pinch-off - or removing drain voltage while the gate is at an operating bias - stresses the
 * devices. This test drives the sequence with callbacks that record every operation, then
 * asserts the order, and asserts that every failure path lands in the safe state (gate off,
 * drain off).
 */
#include "PA_BIAS_SEQUENCE.h"
#include <stdio.h>
#include <string.h>

#define LOG_MAX 16
static char log_[LOG_MAX][24];
static int log_n;
static bool fail_gate_off, fail_gate_op, fail_drain, fail_drain_off;

static void rec(const char *what) { if (log_n < LOG_MAX) snprintf(log_[log_n++], 24, "%s", what); }
static bool gate(uint8_t code)
{
    char b[24];
    snprintf(b, sizeof b, "gate=%u", (unsigned)code);
    rec(b);
    if (code == 10 && fail_gate_off) return false;
    if (code == 100 && fail_gate_op) return false;
    return true;
}
static bool drain(bool on)
{
    rec(on ? "drain=on" : "drain=off");
    if (on && fail_drain) return false;
    if (!on && fail_drain_off) return false;
    return true;
}
static void delay(uint32_t ms) { (void)ms; rec("settle"); }

static int checks, failures;
static void check(bool ok, const char *what)
{
    checks++;
    if (!ok) { failures++; printf("   FAIL: %s\n", what); }
}
static bool seq_is(const char *a, const char *b, const char *c, const char *d)
{
    return log_n == (d ? 4 : (c ? 3 : 2))
        && strcmp(log_[0], a) == 0 && strcmp(log_[1], b) == 0
        && (!c || strcmp(log_[2], c) == 0) && (!d || strcmp(log_[3], d) == 0);
}
static void reset(void) { log_n = 0; fail_gate_off = fail_gate_op = fail_drain = fail_drain_off = false; }

int main(void)
{
    PA_BiasSequence_IO_t io = { gate, drain, delay };
    PA_BiasSequence_Params_t p = { .gate_off_code = 10, .gate_operating_code = 100, .settle_ms = 5 };

    printf("=== enable: gate off, drain up, settle, then gate to the operating point ===\n");
    reset();
    check(PA_BiasSequence_Apply(&io, &p, true), "enable reports success");
    check(seq_is("gate=10", "drain=on", "settle", "gate=100"),
          "order is gate-off -> drain-on -> settle -> gate-operating");

    printf("=== disable: gate off before the drain is removed ===\n");
    reset();
    check(PA_BiasSequence_Apply(&io, &p, false), "disable reports success");
    check(seq_is("gate=10", "drain=off", NULL, NULL),
          "order is gate-off -> drain-off (gate first, always)");

    printf("=== failure paths land in the safe state ===\n");
    reset(); fail_gate_off = true;
    check(!PA_BiasSequence_Apply(&io, &p, true), "a failed gate-off aborts the enable");
    check(log_n == 1 && strcmp(log_[0], "gate=10") == 0, "and the drain was never energised");

    reset(); fail_drain = true;
    check(!PA_BiasSequence_Apply(&io, &p, true), "a failed drain-on aborts the enable");
    check(log_n == 3 && strcmp(log_[2], "gate=10") == 0, "and the gate is driven back to off");

    reset(); fail_gate_op = true;
    check(!PA_BiasSequence_Apply(&io, &p, true), "a failed operating-bias write aborts");
    /* six operations: gate-off, drain-on, settle, the failed write itself, then recovery */
    check(log_n == 6 && strcmp(log_[3], "gate=100") == 0 &&
          strcmp(log_[4], "gate=10") == 0 && strcmp(log_[5], "drain=off") == 0,
          "and it returns to gate-off then drain-off");

    reset(); fail_drain_off = true;
    check(!PA_BiasSequence_Apply(&io, &p, false), "a failed drain-off is reported");
    check(log_n == 2 && strcmp(log_[0], "gate=10") == 0, "and the gate was already safe");

    printf("=== force-off is gate first, drain off, and needs no settle ===\n");
    reset();
    check(PA_BiasSequence_ForceOff(&io, &p), "force-off succeeds");
    check(seq_is("gate=10", "drain=off", NULL, NULL), "force-off order");

    printf("=== argument checking ===\n");
    reset();
    check(!PA_BiasSequence_Apply(NULL, &p, true), "NULL io is refused");
    check(!PA_BiasSequence_Apply(&io, NULL, true), "NULL params is refused");
    check(log_n == 0, "and nothing was driven");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
