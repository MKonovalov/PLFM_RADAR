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
first and `EN_+3V3_ADAR12/34` a moment later would produce the negative first and **violate** this.
(The datasheet's other sequencing sentence — "the −5 V supply powering up first before the PA VDD
is powered on" — is about an *external* PA's drain, i.e. the gate-bias rule, not about AVDD3.)

**The firmware already does this correctly.** `main.cpp` disables the TX mixers, asserts
`EN_P_3V3_ADAR12`/`EN_P_3V3_ADAR34`, waits 500 ms, and only then asserts `EN_P_5V0_ADAR` (which is
what creates the −5 V rails through the U20/U21/U36/U37 inverters). The requirement is now cited
in a comment at that site and **locked by a test**: `test_adar_power_order.c` reads the source and
asserts the offsets are ascending, so a later edit cannot silently swap them. Swapping the two
blocks makes it fail with two failures, which is how it was verified.

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
  than a missing-enable question. The enables are driven by the MCU (`U2`) over the harness
  (`SV1`), and the firmware asserts the 3.3 V rails before the 5.0 V rail that creates the −5 V —
  the compliant order, cited at the call site and locked by `test_adar_power_order.c`.
* The ADTR1107 pair is compliant as built.

## Still a bench step

The issue's second acceptance criterion — a scope capture of the rails at power-up — needs
hardware. This document is the first half: the intended order and the citations that justify it.

## The switch and negative rails: why the "unstaged" converters are not a violation (issue #20)

Three converters have their enable tied to their input supply, so they start the moment VIN is
present rather than under sequence control:

```
U3   VIN,EN on the same net  ->  +3V3
U11  VIN,EN on the same net  ->  +3V4
U12  VIN,EN on the same net  ->  +5V0_0
```

A gate finds **thirteen** such converters, and every one of them is a **positive** rail. That is
the whole point, and it is why they are not a sequencing defect.

### The negative rails are charge pumps, and a charge pump has no enable

Every negative rail on this board comes from an LM2662 switched-capacitor inverter, which runs
whenever its V+ is present:

| Pump | Produces | Fed from | That source is |
|---|---|---|---|
| `U18` | −3V3_SW | **+3V3_SW** | staged (`U10`, MCU `PE14`) |
| `U19` | −3V4 | **+3V4** | unstaged (`U11`) |
| `U20`/`U21`/`U36`/`U37` | −5V0_ADAR12/34 | **+5V0_ADAR** | staged (`U13`, MCU `PE10`) |
| `U22` | −5V5_PA | **+5V5_PA** | staged (`U17`) |

A negative rail therefore **cannot precede the positive rail feeding it** — the pump has nothing to
invert until that rail exists. The order is self-enforcing rather than dependent on sequencing
logic, and it holds even for the unstaged positives.

### That is exactly what the GaAs parts require

The ADTR1107 datasheet states its bias sequence explicitly, and the positive switch rail comes
**first**:

> **Transmit power-up:** 1. connect all GND pins. 2. Set the VDD_SW pin to 3.3 V. 3. Set the
> VSS_SW pin to −3.3 V. … 8. Set the VDD_PA pin to 5 V. 9. Increase VGG_PA to achieve the desired
> IDQ_PA. 10. Apply the RF signal.

> **Transmit power-down:** 1. Turn off the RF. 2. Decrease VGG_PA to −1.75 V. 3. Set VDD_PA to 0 V.
> 4. Set VSS_SW to 0 V. 5. Set VDD_SW to 0 V.

The design meets this: `+3V3_SW` (VDD_SW) is staged on `PE14`, and `−3V3_SW` (VSS_SW) is inverted
from it, so the positive necessarily arrives first. The same pattern holds for the ADAR rails,
where `+3V3_ADAR12/34` and `+5V0_ADAR` are staged and the −5 V rails are inverted from `+5V0_ADAR`.

The power-down order is the firmware's business, and it is the reverse of the power-up order —
which is what `PA_BIAS_SEQUENCE` implements for the PA drains and gates.

### Locked by a gate

`9_Firmware/tools/power_sequencing.py` checks that every charge pump draws from a positive rail of
its own family, and reports the unstaged converters so the list stays visible. Verified by fault
injection: re-sourcing `U18` from a negative rail produces
`U18: -3V3_SW is fed from -3V4, which is not a positive rail` and a non-zero exit.

### What remains

The scope capture in the acceptance criteria is a bench step: probe the rails at power-up and
confirm the order in practice. The design side is settled and now enforced by the gate.
