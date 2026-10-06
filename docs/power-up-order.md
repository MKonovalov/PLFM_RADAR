# Power-up order (issue #20)

The power board brings three converters up with their enable tied to their input, so they start
whenever VIN appears rather than under sequence control. This document does two things: it
records the order the design actually produces, and it checks that order against the requirements
of the devices those rails feed — from their own datasheets, not from reasoning about the circuit.

## What the rails feed

| Rail | Generator | Staged? | Loads that matter |
|---|---|---|---|
| `+3V3` | `U3` (TPS562208, EN tied to VIN) | **no** | the MCU (`U2`), the PG pull-ups |
| `+3V4` | `U11` (TPS562208, EN tied to VIN) | **no** | `M3SWA2-34DR+` × 17 (VDD), and `U19` inverts it to `-3V4` |
| `+5V0_0` | `U12` (TPS562208, EN tied to VIN) | **no** | `AD8352ACPZ-R7` × 2 (VCC) |
| `+5V0_ADAR` | `U13` (EN `EN_+5V0_ADAR` via SV1) | **yes** | `U20`/`U21`/`U36`/`U37` pumps → `-5V0_ADAR12/34` |
| `+3V3_ADAR12/34` | `U6`/`U7` (EN via SV1) | **yes** | `ADAR1000` × 4 (AVDD3) |
| `+3V3_SW` | `U10` (EN via SV1) | **yes** | `U18` inverts it to `-3V3_SW` |
| `-3V4`, `-5V0_ADAR12/34`, `-3V3_SW` | LM2662 inverters | derived | the negative inputs above |

Two structural facts follow, and they constrain what any sequencing can achieve:

1. **Every negative rail is derived from a positive one** (an LM2662 fed by `+3V4`, `+5V0_ADAR`
   or `+3V3_SW`). "Negative rail first" is therefore not physically available for those pairs —
   the positive must exist for the negative to exist.
2. The ADAR1000's pair is nevertheless **fully controllable**, because both of its rails are
   staged: `+5V0_ADAR` (which creates `-5V0_ADAR12/34`) and `+3V3_ADAR12/34` are separate
   enables, so the order between them is a firmware/harness decision.

## What the devices require (cited)

**ADAR1000** — pin table, `AVDD3` (M10, M11, N11):

> "3.3 V Voltage Power Supply Inputs. It is recommended to power-up these pins before or at the
> same time as the AVDD1(-5V) supply."

So `+3V3_ADAR12/34` must be asserted **no later than** `+5V0_ADAR`. Asserting `EN_+5V0_ADAR`
first and `EN_+3V3_ADAR12/34` a moment later produces the negative first and **violates** this.
(The datasheet's other sequencing sentence — "the −5 V supply powering up first before the PA VDD
is powered on" — is about an *external* PA's drain, i.e. the gate-bias rule, not about AVDD3.)

**ADTR1107** — "Recommended Bias Sequencing" (Transmit and Receive, power-up and power-down) puts
`VDD_SW` (+3.3 V) first and `VSS_SW` (−3.3 V) alongside; the part is specified at
`VDD_PA = 5 V, VDD_SW = 3.3 V, VSS_SW = −3.3 V`. The design's `+3V3_SW` is staged and `-3V3_SW` is
inverted from it, so this pair already comes up positive-first, which is what the part asks for.

**M3SWA2-34DR+** — specified at `VDD = +3.3 V, VEE = −3.3 V` (max +3.6 / −3.6 V), drawing 2.7 mA
and 1.6 mA per switch. `+3V4` is the source rail and `U19` inverts it, so the pair is
positive-first by construction. The absolute-maximum table is the one item here still to be
pinned down from the datasheet (the extracted text carried the heading but not the limits);
until then this pair is recorded as "positive-first by construction, abs-max to confirm".

**LTC5552** (× 2) and **AD8352** (× 2) — single-supply parts (`+3V3_AN1_F` and `+5V0_0`
respectively), so they impose no order requirement on their rails.

## Verdict

* `+3V3`, `+3V4`, `+5V0_0` being unstaged is **acceptable**: their loads are a microcontroller, a
  dual-supply switch whose negative is derived from the positive, and a single-supply amplifier.
  None of them specifies an order the design breaks.
* The **ADAR1000 pair is the real constraint**, and it is a sequencing *order* question rather
  than a missing-enable question: `EN_+3V3_ADAR12/34` must not be asserted after `EN_+5V0_ADAR`.
  Whichever side drives those enables (they arrive over the harness, `SV1`) must assert them
  together or in the +3.3 V-first order.
* The ADTR1107 pair is compliant as built.

## Still a bench step

The issue's second acceptance criterion — a scope capture of the rails at power-up — needs
hardware. This document is the first half: the intended order and the citations that justify it.
