# The eight temperature channels (issue #5)

The issue said the firmware declares eight temperature sensors and no board carries one, and that
the harness has no spare position for them. The first half was right; the second turned out to be
the wrong question — **the interface already exists on the main board**, and what is missing is a
pull-up and the probes themselves.

## What the netlists actually contain

* **Three ADS7830s**, all on `I2C2`, all ordered in the BOM:

  | Reference | Address straps | I²C address | Role |
  |---|---|---|---|
  | `U10` | A1, A0 tied to GND | 0x48 | `hadc1` — the first eight Idq channels |
  | `U88` | A0 tied to GND, A1 pulled up by `R151` | 0x4A | `hadc2` — the second eight Idq channels |
  | `U89` | A1 tied to GND, A0 pulled up by `R153` | 0x4B | `hadc3` — the temperature channels |

* **Eight 3-pin headers** — `JP5`, `JP6`, `JP11`, `JP12`, `JP14`, `JP15`, `JP16`, `JP19` — each
  carrying `+3V3_AN4_F` on pin 1, one `U89` channel on pin 2, and `GND` on pin 3. That is a
  complete three-wire sensor interface, already routed and already in the BOM.

So the harness was never the constraint: the probes are external, on flying leads, and plug into
these headers. They belong at the PA, which is where the temperature is actually wanted.

## What was missing

1. **No pull-up.** The channel nets (`N$295`, `N$296`, `N$297`, `N$298`, `N$299`, `N$301`,
   `N$306`, `N$307`) carried only the header pin and the ADC pin, so an NTC on the header would
   have had nothing to divide against. Eight 10 kΩ 0201 resistors (`R196`–`R203`) now pull each
   channel up to `+3V3_AN4_F`, reusing the repo's own `rcl`/`R-EU_`/`R0201` deviceset.
2. **No names.** The channels were anonymous `N$…` nets. They are now `TEMP_1`…`TEMP_8`, in
   `U89`'s channel order, so the firmware's index maps straight onto the net name.
3. **A firmware bug.** `hadc3` was read eight times per cycle and **never initialised** — only
   `hadc1` (0x48) and `hadc2` (0x4A) were. Every `Temperature_N` was reading an unconfigured part.

## The channel map

| Channel | Net | Header |
|---|---|---|
| CH0 | `TEMP_1` | `JP19` |
| CH1 | `TEMP_2` | `JP14` |
| CH2 | `TEMP_3` | `JP15` |
| CH3 | `TEMP_4` | `JP5` |
| CH4 | `TEMP_5` | `JP6` |
| CH5 | `TEMP_6` | `JP11` |
| CH6 | `TEMP_7` | `JP12` |
| CH7 | `TEMP_8` | `JP16` |

## The probes

A 10 kΩ NTC with B(25/85) = 3434 K in a 0201 case, on the header's pin 2 and pin 3 (signal and
ground). Because the pull-up and the ADC reference are the same rail, the ADC code **is** the
divider ratio and the absolute rail voltage cancels — no reference measurement is needed.

`PA_Thermal_NtcToCelsius()` in `PA_THERMAL.c` inverts the divider and applies the B-equation.
It returns false for a reading that cannot be a real temperature, and the case that matters is an
**unplugged probe**, which sits on the rail and would otherwise read as a very cold board. The
firmware keeps the last good value and flags the channel instead.

## The firmware's conversion was wrong in two different ways

`Temperature_1..8` are floats formatted as `%.1f`, so they are meant to hold degrees:

```c
Temperature_1 = ADS7830_Measure_SingleEnded(&hadc3, 0);            // bring-up: a raw 0..255 code
Temperature_1 = ADS7830_Measure_SingleEnded(&hadc3, 0)*0.64705f;   // loop: a linear scale
```

Neither is right for a thermistor, and the two disagreed with each other. Both sites now call one
reader, `Read_PA_Temperatures()`, which converts properly.

## What the model says

With a 10 kΩ pull-up equal to the probe's R25, the divider midpoint is exactly 25 °C — so a ratio
of 0.5 is a self-check of the whole conversion, not just the arithmetic. The measured points:

```
ratio 0.10 ->  95.3 C     ratio 0.50 ->  25.0 C
ratio 0.30 ->  48.7 C     ratio 0.90 -> -22.8 C
```

Note the first: **ratio 0.10 lands on 95.3 °C**. That was the case limit derived from the QPA2962's
θJC — 95.4 °C — so the protection threshold and the sensor's useful range coincided, which is what
suggested the divider was dimensioned for this job.

**With the QPA1010 the coincidence lapses, and in the safe direction.** Its θJC is 2.60 °C/W and its
planning dissipation 24.8 W, so the 200 °C channel design limit is not reached until a case of
**135 °C** — and the datasheet's own T_BASE maximum of **85 °C** binds far earlier than either. The
sensor's 95 °C ceiling therefore still sits above every limit that matters, and the firmware's 75 °C
trip keeps the channel at 139 °C. The divider does not need re-dimensioning; what changed is which
limit is binding, and the sensor is still on the correct side of it.

## Still to do

Fit the eight probes to the PA boards (or wherever the case temperature is best measured) and plug
them into the headers. Nothing else is outstanding on the board: the interface, the pull-ups, the
addresses, the ADC initialisation and the conversion are all in place.
