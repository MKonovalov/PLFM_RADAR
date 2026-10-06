/**
 * @file    RAIL_FAULT.h
 * @brief   Rail-good monitoring: sample, latch, report.
 *
 * Three TPS7A8300 regulators expose PG (active-high, open-drain, asserting when the output
 * reaches 89 % of target). Each now has a 10 kohm pull-up on the power board and its own
 * 2-position connector to the MCU, where the pin is read with the internal pull-down.
 *
 * That arrangement is fail-safe, and it is worth writing down because the obvious reading of
 * "open-drain" gets it backwards:
 *
 *   harness connected, rail good  -> the pull-up wins over the MCU's pull-down  -> HIGH
 *   harness connected, rail bad   -> the regulator pulls the line low           -> LOW
 *   harness disconnected          -> only the MCU's pull-down remains           -> LOW
 *
 * So LOW means "this rail is bad **or** it is not being monitored", and both are conditions a
 * bring-up wants reported. HIGH is the only state that means good.
 *
 * The latch matters: a rail that dips and recovers has still told you something, and a
 * brown-out that leaves no trace is the failure this whole item exists to prevent.
 */
#ifndef RAIL_FAULT_H
#define RAIL_FAULT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** One monitored rail. `is_ok` is the raw pin level: true = HIGH = good. */
typedef struct {
    bool (*is_ok)(void);
} RailFault_Source_t;

typedef struct {
    const RailFault_Source_t *sources;
    uint8_t count;          /**< number of sources, <= 32 */
    uint32_t latched;       /**< bit per rail: 1 = a fault has been seen since the last clear */
    uint32_t current;       /**< bit per rail: 1 = faulting right now */
} RailFault_Monitor_t;

/** Bind a monitor to its sources. Returns false if the binding is unusable. */
bool RailFault_Init(RailFault_Monitor_t *mon, const RailFault_Source_t *sources, uint8_t count);

/** Sample every rail once. Returns true if any rail is faulting now. */
bool RailFault_Sample(RailFault_Monitor_t *mon);

/** True if a fault has been seen since the latch was cleared. */
bool RailFault_HasLatched(const RailFault_Monitor_t *mon);

/** Clear the latch (deliberate, so it cannot be cleared by accident). */
void RailFault_ClearLatch(RailFault_Monitor_t *mon);

#endif /* RAIL_FAULT_H */
