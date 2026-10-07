# The three analysis items: #5 (thermal requirement), #19 (inrush), #21 (migration)

These three are not bench steps waiting on hardware — they are questions with enough data in hand
to answer now. Each answer is a number or a choice, not a procedure.

## #5 — the thermal requirement, as a specification

The issue's finding is that nothing in the design or docs states a thermal requirement: no heatsink,
no airflow figure, no duty limit. The arithmetic sets one.

Per device, from the QPA2962 brief:

```
P_DISS  = 36.96 W      (VD = 22 V, IDQ = 1680 mA, no RF)
theta_JC = 2.83 degC/W
T_CH    = 189 degC     at T_BASE = 85 degC
```

The array is sixteen devices, so **592 W** is dissipated in total. Working from the datasheet's own
characterized condition — a case at **85 °C** — and a 40 °C ambient inside an enclosure:

| | per device | array |
|---|---|---|
| dissipation | 36.96 W | 592 W |
| allowed rise (85 − 40 °C) | 45 K | 45 K |
| required case-to-ambient θ | **1.22 °C/W** | **0.076 °C/W** |

For scale: a good forced-air heatsink is around 0.5–1 °C/W, and a liquid-cooled plate reaches
0.05–0.1 °C/W. So **at continuous duty this array needs liquid cooling or a very large forced-air
assembly** — that is the requirement the design was missing.

There is a second, cheaper answer, and it is the one the earlier duty-cycle estimate pointed at. At
the ~22 % duty that keeps each device near 8 W average, the required θ relaxes to about
**5.6 °C/W per device**, which ordinary air cooling meets comfortably.

**So the requirement is a choice, stated either way:**

* continuous duty → θ_case-ambient ≤ 0.076 °C/W for the array (liquid-cooled plate), or
* ≤ ~22 % duty → θ ≤ 5.6 °C/W per device (forced air is sufficient).

The channel limit itself is not in doubt: the case may reach 95.4 °C before the channel approaches
200 °C, and the firmware's 75 °C PA-sensor limit keeps it near 180 °C. `PA_THERMAL.h` holds that
model and `test_pa_thermal.c` checks it against the datasheet's own 189 °C row.

## #19 — the inrush, bounded, with the one number that decides it

The `+3V3_XO` rail carries the OCXO plus the rest of its domain, about **1.25 A** steady, on a
**TPS7A8300 rated 2 A** — roughly **62 % loaded before any warm-up transient**. So the headroom is
about **0.75 A**.

The 88 µF of bulk on that rail does **not** cover a warm-up surge, and it is worth showing why:

```
I*t/C = 500 mA * 1 ms / 88 uF = 5.7 V of droop
```

To hold a 1 ms surge to even 100 mV would need **5000 µF** — so bulk is not the answer here; the
regulator has to source it. That makes the deciding number the OCXO's warm-up current, which is not
in the datasheet's extractable text (the tables are images) and has to be read from the document or
asked of ECS.

**The requirement, stated so it can be checked:** total rail current during warm-up must stay
**≤ 2.0 A**, i.e. the OCXO's warm-up surge must not exceed about **0.75 A** above the rest of the
domain. If it does, the fix is a higher-current regulator for that domain, not more capacitance.

## #21 — the QPA1010 migration: it is not a drop-in, and it makes #5 harder

The candidate is a real part with published numbers:

| | QPA2962 (current) | QPA1010 (candidate) |
|---|---|---|
| band | 10.5 GHz operation | **7.9 – 11 GHz** — covers it |
| saturated output | ~10 W | **15 W** |
| technology | GaN | GaN on SiC |
| gate bias | negative, VGG then VD | same class — negative, gate then drain |

So the band is covered and the bias *sequence* is the same kind of thing the firmware already
implements (`PA_BIAS_SEQUENCE`, and the ADTR1107's documented order in `docs/power-up-order.md`).

**But it is not a like-for-like swap, and the interaction matters:** 50 % more saturated output
means **more dissipation**, not less — which tightens the #5 thermal requirement above rather than
easing it. Any decision to migrate should be made with that arithmetic in hand, not after.

**What the decision actually turns on** (and why it is the owner's, not mine):

1. **Export status.** The issue's premise is that the QPA2962 is export-restricted. Both parts are
   Qorvo GaN, so this has to be established for the QPA1010 specifically rather than assumed to be
   better — that is the whole point of the migration.
2. **Sourcing.** Sixteen devices; the migration is only worth doing if the replacement is actually
   obtainable in that quantity.
3. **Thermal headroom.** Whether the array can take the extra dissipation, which is the #5 choice
   above.

The bias-table port itself is the small part of the work: `PA_GATE_BIAS.h` expresses VGG as
`-(Rf/Rin)*(code/255)*VREF` with a −1.20 V floor, and a new device needs a new table and a new
floor — the mechanism is unchanged.
