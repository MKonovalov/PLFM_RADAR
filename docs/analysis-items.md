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

## Correction: #19's numbers, from the vendor datasheet

The issue was built on "the ECOC-2522 OCXO at ~1.2 A". The datasheet gives the figure directly, and it is
much lower:

| Parameter | Datasheet | At 3.3 V |
|---|---|---|
| Power consumption, at turn on | **3.6 W** | **1.09 A** |
| Power consumption, steady state | **1.4 W** | **0.42 A** |

So the rail is **21 % loaded steady and 55 % at turn-on** — not 62 % — against a 2 A regulator. The
acceptance criterion "no rail above ~70 % of its device rating" is met with room to spare, and there is no
current-limit droop to avoid.

Sequencing the OCXO alone is also not available: the padout is 1 = Voltage Control, 2 = V Ref,
3 = Supply Voltage, 4 = Output, 5 = Ground — **no enable pin**. It would need an added load switch, which
the numbers show is unnecessary.

Two earlier claims in this repository are corrected by the same table: that no vendor data was reachable,
and that the datasheet's tables were image-based. Both were wrong; the PDF extracts cleanly.

## #5 addendum: the airflow figure the issue asks for

The issue's "what is needed" list includes "a documented airflow figure". Computed for the array
(16 devices at 36.96 W = 591 W), with a 40 °C ambient, an 85 °C case target, and a 15 K rise allowed
in the air (ρ = 1.2 kg/m³, cp = 1005 J/kg·K):

| duty | dissipated | required airflow | θ case→ambient, array | per device |
|---|---|---|---|---|
| **10 % (the acceptance case)** | 59 W | **≈ 7 CFM** | **0.76 °C/W** | 12.2 °C/W |
| continuous | 591 W | **≈ 69 CFM** | 0.076 °C/W | 1.2 °C/W |

The acceptance soak is specified at **10 % duty**, and that row is the practical one: **a normal
heatsink with a small fan** (12 °C/W per device, ~7 CFM) meets it with margin. The continuous row is
what forces liquid cooling or a large forced-air assembly, and it is the reason the requirement has
to be stated as a choice rather than a single number.

The baseplate thermistor the issue also asks for is already provided for: the eight NTC channels on
the main board are external flying-lead probes on 3-pin headers (JP5/JP6/JP11/JP12/JP14/JP15/JP16/JP19),
so one can be attached to the baseplate and read by the MCU with no further hardware — see
`docs/temperature-channels.md`.

## #21 CORRECTION: the QPA1010 dissipates about HALF as much, not more

The earlier analysis in this file said the migration "tightens #5 rather than easing it", reasoning
from 15 W against 10 W. **That was wrong**, and the datasheet says so plainly.

| | QPA2962 (current) | QPA1010 (candidate) |
|---|---|---|
| Drain voltage (VD) | 22 V | **24 V** |
| Quiescent drain current (IDQ) | 1680 mA | **600 mA** |
| Gate voltage range (VG) | −1.2 to −2.5 V | **−2.9 to −1.5 V** |
| **Power dissipation, driven** | **36.96 W** | **17.7 W** |
| Channel temperature at TBASE = 85 °C | 189 °C | **131 °C** |
| θJC (derived from the two rows above) | 2.83 °C/W | **2.60 °C/W** |
| Power-added efficiency | 22 % | **38 %** |
| Saturated output | ~10 W | 15 W (42 dBm at PIN = 24 dBm) |
| Package | — | 24-lead 4.5 × 5.0 × 1.72 mm air-cavity laminate |
| Compliance | — | **Lead-free and RoHS compliant** |

The reason is efficiency: 38 % PAE against 22 % means that for **more** output power it dissipates
**less**. The QPA1010's own thermal row gives PDISS = 17.7 W at 41.4 dBm output, against the
QPA2962's 36.96 W.

**What that means for the array:**

| | QPA2962 | QPA1010 |
|---|---|---|
| array dissipation (×16) | 592 W | **283 W** |
| required θ case→ambient, continuous | 0.076 °C/W | **0.159 °C/W** |
| required airflow, continuous | 69 CFM | **34 CFM** |

So the migration would roughly **halve** the thermal load and make the #5 requirement considerably
easier — possibly bringing continuous duty within reach of air cooling rather than liquid cooling.
It also removes a procurement constraint: the part is RoHS compliant, which the FLP-1250 filter is
not (see `docs/harmonic-filter.md`).

Two other differences worth noting for the port: IDQ drops from 1680 mA to 600 mA, which changes the
bias table's operating point, and the gate range shifts to −2.9…−1.5 V. The *sequence* is unchanged
in kind — negative gate, then drain — which is what `PA_BIAS_SEQUENCE` and the ADTR1107's documented
order already implement.
