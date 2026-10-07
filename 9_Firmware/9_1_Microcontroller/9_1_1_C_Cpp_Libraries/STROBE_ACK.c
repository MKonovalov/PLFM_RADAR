/**
 * @file    STROBE_ACK.c
 * @brief   MCU side of the strobe acknowledgement.  See STROBE_ACK.h.
 */
#include "STROBE_ACK.h"

void StrobeAck_Init(StrobeAck_State_t *st)
{
    if (st == NULL) {
        return;
    }
    st->ack = false;
    st->sent = 0;
    st->acknowledged = 0;
    st->missed = 0;
}

void StrobeAck_BeforeSend(StrobeAck_State_t *st)
{
    if (st == NULL) {
        return;
    }
    /* Score the request that is about to be superseded.  A request that was never
     * acknowledged is a dropped strobe, and this is the last moment it can be seen: the
     * FPGA clears the line as soon as the new request arrives. */
    if (st->sent > 0 && !st->ack) {
        st->missed++;
    }
    st->sent++;
}

void StrobeAck_Sample(StrobeAck_State_t *st, bool ack)
{
    if (st == NULL) {
        return;
    }
    if (ack && !st->ack) {
        st->acknowledged++;
    }
    st->ack = ack;
}

bool StrobeAck_HasMissed(const StrobeAck_State_t *st)
{
    return st != NULL && st->missed != 0;
}
