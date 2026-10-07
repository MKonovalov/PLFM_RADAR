/**
 * @file    STROBE_ACK.h
 * @brief   Did the FPGA act on my strobe?  (issue #13)
 *
 * The MCU asserts new_chirp and the chirp FSM reads it only in IDLE, so a strobe arriving
 * mid-chirp used to vanish without a trace. The FPGA now answers on one wire: a request clears
 * the acknowledgement, consumption sets it. This module keeps the MCU's side of that bargain -
 * it remembers what it sent and compares, so a dropped strobe becomes a number instead of a
 * silence.
 *
 * The comparison has to happen *before* the next strobe is issued: the FPGA clears the
 * acknowledgement when it sees a new request, so checking after sending the next one would
 * always read low.
 */
#ifndef STROBE_ACK_H
#define STROBE_ACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool    ack;              /**< the line, true = the last request was consumed */
    uint32_t sent;            /**< requests issued */
    uint32_t acknowledged;    /**< requests the FPGA confirmed */
    uint32_t missed;          /**< requests still unanswered when the next one went out */
} StrobeAck_State_t;

/** Reset the bookkeeping. */
void StrobeAck_Init(StrobeAck_State_t *st);

/** Call immediately BEFORE asserting a strobe: it scores the previous request. */
void StrobeAck_BeforeSend(StrobeAck_State_t *st);

/** Call with the current line level while waiting (or after) a request. */
void StrobeAck_Sample(StrobeAck_State_t *st, bool ack);

/** True if any request has gone unanswered. */
bool StrobeAck_HasMissed(const StrobeAck_State_t *st);

/**
 * Bring-up self-test for the acknowledgement path itself (issue #13).
 *
 * The functional case - a strobe dropped because the FPGA was mid-chirp - is what the miss
 * counter above is for. This answers a different question: is the wire alive at all? It works
 * on the same evidence the MCU already has, without extra hardware: raise a strobe, then watch
 * for the acknowledgement within a timeout. A chirp in flight will drop the strobe, so a caller
 * should allow a few attempts before concluding the path is dead.
 */
typedef struct {
    uint32_t timeout_ms;
    uint32_t waited_ms;
    bool     running;
    bool     passed;
} StrobeAck_SelfTest_t;

/** Start a test. The caller is expected to raise a strobe immediately after this. */
void StrobeAck_SelfTest_Begin(StrobeAck_SelfTest_t *t, uint32_t timeout_ms);

/** Result of one poll: still waiting, passed, or timed out. */
typedef enum {
    STROBE_SELFTEST_WAITING = 0,
    STROBE_SELFTEST_PASSED,
    STROBE_SELFTEST_TIMEOUT
} StrobeAck_SelfTestResult_t;

/** Feed the elapsed time and the current line level. */
StrobeAck_SelfTestResult_t StrobeAck_SelfTest_Poll(StrobeAck_SelfTest_t *t, uint32_t elapsed_ms,
                                                   bool ack);

#endif /* STROBE_ACK_H */
