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

#endif /* STROBE_ACK_H */
