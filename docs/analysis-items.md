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

**Re-derived for the QPA1010** (issue #21; the figures above are the QPA2962's and are kept only as
the comparison). The array is sixteen devices at 24 V and 600 mA quiescent, so **230.4 W** standing —
and **396.8 W** under drive, because driven dissipation (24.8 W) exceeds quiescent (14.4 W):

| | per device | array |
|---|---|---|
| dissipation, standing | 14.4 W | 230.4 W |
| dissipation, driven | **24.8 W** | **396.8 W** |
| allowed rise (85 − 40 °C) | 45 K | 45 K |
| required case-to-ambient θ, standing | **3.13 °C/W** | **0.195 °C/W** |
| required case-to-ambient θ, **driven** | **1.81 °C/W** | **0.113 °C/W** |

**Driven is the binding case** — the opposite of the old device, where quiescent was the larger
figure. A plan written against the standing number would be planning for 1.7× less dissipation than
the part actually produces under drive.

**What that means against the board.** The via field is worth ~0.77–1.15 °C/W per device, so against
the driven requirement of 1.81 °C/W it leaves roughly **0.7–1.0 °C/W** for the thermal interface and
the heatsink. A good forced-air heatsink is 0.5–1 °C/W, so **forced air suffices under drive** — and
at the radar's 10 % transmit duty, where the drain is pulsed per the datasheet's own test conditions
(PW = 100 µs, DC = 10 %), the average load is far lower again.

The old requirement was 1.22 °C/W per device that the same field met *with no margin*, and 0.076 °C/W
for the array which needed a liquid-cooled plate. **The migration removes that** — it is the single
largest practical consequence of the device change, and it is worth stating plainly because the
issue's own framing ("the binding constraint is arithmetic not copper") stops being true.

**Airflow.** The old 69 CFM / 7 CFM figures were derived against 592 W and no longer apply. The
requirement falls by roughly the ratio of the dissipations (2.6×), which puts it in the range of a
small fan rather than a blower — but the exact figure follows the chosen heatsink's own θ-versus-flow
curve, so it is stated as a scaling here and pinned when a heatsink is chosen.

The channel limit itself is not in doubt: `PA_THERMAL.h` now carries the QPA1010's θJC of 2.60 °C/W,
which puts the channel at 150 °C for an 85 °C case at the driven dissipation, and reaches the 200 °C
design limit only at a case of 135 °C. `test_pa_thermal.c` checks the model against the datasheet.

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


**Re-derived for the QPA1010** with the same formula and the same 15 K air rise:

| duty | dissipated | required airflow | θ case→ambient, array | per device |
|---|---|---|---|---|
| standing, drain up, no RF | 230 W | **≈ 27 CFM** | **0.20 °C/W** | 3.1 °C/W |
| driven, 15 W RF out | 397 W | **≈ 46 CFM** | **0.11 °C/W** | 1.8 °C/W |
| **10 % duty (the acceptance case)** | 23 W | **≈ 3 CFM** | **1.96 °C/W** | 31.3 °C/W |

**What that changes.** The continuous row is the one that mattered: the previous device needed
**69 CFM** at continuous duty, which is why the earlier conclusion was that the array needed
*liquid cooling or a very large forced-air assembly*. At **27 CFM** the QPA1010 is served by an
ordinary fan on a normal heatsink, and the acceptance case falls to about **3 CFM**.

So the airflow requirement is met by the simplest possible answer, and the pedestal/copper coin
the issue also asks for stop being prerequisites and become margin. The per-device column is the
figure to compare against the board's via field (~0.77–1.15 °C/W).

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
| array dissipation (×16), standing | 592 W | **230 W** |
| array dissipation (×16), driven | 473 W | **397 W** |
| required θ case→ambient, standing | 0.076 °C/W | **0.195 °C/W** |
| required θ case→ambient, **driven (binding)** | 0.095 °C/W | **0.113 °C/W** |
| required airflow | 69 CFM at 592 W | falls ~2.6×; see the re-derivation above |

**The 283 W in the earlier version of this table was wrong** — see the correction note below, which
explains where it came from. The figures here are recomputed from the QPA1010's own bias point.

So the migration would roughly **halve** the thermal load and make the #5 requirement considerably
easier — possibly bringing continuous duty within reach of air cooling rather than liquid cooling.
It also removes a procurement constraint: the part is RoHS compliant, which the FLP-1250 filter is
not (see `docs/harmonic-filter.md`).

Two other differences worth noting for the port: IDQ drops from 1680 mA to 600 mA, which changes the
bias table's operating point, and the gate range shifts to −2.9…−1.5 V. The *sequence* is unchanged
in kind — negative gate, then drain — which is what `PA_BIAS_SEQUENCE` and the ADTR1107's documented
order already implement.

## #21 — the decision inputs, researched

The issue's question is whether to migrate the PA from the QPA2962 to the QPA1010. Three of the four
inputs turn out to point the same way, and one of them is new.

### Export classification — from Qorvo's own ECCN tool, not a distributor

Qorvo publishes an ECCN lookup and its page is backed by a public endpoint
(`/api/pim/SearchEccnData?partNumber=…`), so this is the manufacturer's own classification:

| Orderable part | ECCN | HTS |
|---|---|---|
| `QPA2962`, `QPA2962S2`, `QPA2962TR7` | **3A001.B.2.C** | 8542330001 |
| `QPA1010TR7`, `QPA1010D` | **3A001.B.2.B.2** | 8542330001 |
| `QPA2962EVB`, `QPA1010PCB4B01` (evaluation boards) | **EAR99** | 9030820000 |

**Both parts are export-controlled.** The migration does not buy an easier classification; it moves
from the general clause to the one high-power X-band GaN amplifiers carry. Evidence for how the two
paragraphs are applied in practice: Amcom's 8 W 2–18 GHz, MACOM's X-band 12 W and Qorvo's own
TGA2625 all carry `3A001.b.2.b.2`, while broadband driver MMICs (Hittite's, `CMD304`, `MMA-172135D`)
carry `3A001.b.2.c`. **Stated as the manufacturer's classification plus the vendor pattern, not as
the letter of the regulation**, which was not read in full.

Worth noting for evaluation: **the evaluation boards are EAR99** even though the devices are not.

### Availability — and this one reverses the picture

At the time of checking:

- **`QPA2962` — Mouser: "Lifecycle: Restricted. Availability: This part number is not currently
  available from Mouser."** The incumbent is hard to obtain.
- **`QPA1010TR7` — DigiKey: "Buy now, ships today."** The candidate is in stock.

So the part in the current design is the one with the availability problem. That is a stronger
argument for evaluating the migration than either the thermal or the RoHS point.

### Minimum order

- the candidate's **packaged** variant is `QPA1010TR7` only — a **250-piece reel**;
- the current part offers `S2` (2), `(25)` and `TR7` (250) — small quantities, when available;
- a die option exists (`QPA1010D`, ≈ $292 each at 10 pieces, not stocked at DigiKey).

A 16-device array therefore means buying a 250-piece reel, or using die.

### The rest, from the earlier analysis

| | QPA2962 | QPA1010 |
|---|---|---|
| dissipation per device, standing | 36.96 W | **14.4 W** (24 V × 0.6 A quiescent) |
| dissipation per device, driven | 35.5 W | **24.8 W** (P_DC 39.8 W − 15 W RF, at 37.7 % PAE) |
| array, standing (×16) | 592 W | **230 W** |
| required θ, array (40 → 85 °C) | 0.076 °C/W | **0.196 °C/W** |
| required θ per device | 1.22 °C/W | **3.13 °C/W** |
| via field provides | 0.77–1.15 °C/W | 0.77–1.15 °C/W → **2.7–4× margin** |
| channel temperature at 85 °C base | 189 °C | **150 °C** (85 + 2.60 × 24.8) |

**Correction to an earlier figure in this document.** A previous version of this table said the
QPA1010 array dissipates **283 W**. That was wrong: 16 × 24 V × 0.6 A = **230 W** standing. The
error came from scaling the old device's array figure by a ratio rather than recomputing it from
the new device's own bias point — the same shortcut that the row-by-row figures above avoid. The
direction of the conclusion is unchanged (the migration roughly halves the standing load), but the
number quoted on issue #21 was not the number the arithmetic gives, and it is corrected here.

**And the safe planning figure changes side.** For the QPA2962 the *quiescent* dissipation (36.96 W)
exceeded the driven one (35.5 W), so quiescent was conservative. Here **driven (24.8 W) is nearly
twice quiescent (14.4 W)**, so driven is the figure to plan with. Anyone carrying the old habit
across would plan against half the real load — which is why `PA_THERMAL.h` now derives
`PA_PDISS_PLAN_W` as the larger of the two rather than naming one.

The device is also specified **pulsed** (PW = 100 µs, DC = 10 %), which is the radar's own duty, so
under a 10 % transmit the drain is pulsed and the average load is far below the standing figure.
| VD / IDQ | 22 V / 1680 mA | 24 V / 600 mA |
| gate range | −1.2 … −2.5 V | −2.9 … −1.5 V |
| RoHS | — | **compliant** (the chosen filter is not) |

### What the decision actually turns on

Migrating would **halve the thermal load**, remove the RoHS gap, and use a part that is **in stock**
rather than one that is not. Against that: a **new bias operating point** (a firmware change), a
**250-piece reel** minimum, and a **different ECCN sub-paragraph** — not an easier one.
