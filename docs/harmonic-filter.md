# Post-PA harmonic filter (issue #7)

## Why

Nothing follows the PA before the antenna port. `aeris10_bpf_spec.md` states the requirement
this leaves open:

> TX instance additionally: 21.0 / 31.5 GHz — **≥ 35 dB** if the filter is used as the TX
> harmonic stop (2nd/3rd harmonic)

and the prefab checklist quantifies the consequence: at +40 dBm output with −20 dBc harmonics,
the unfiltered 21 GHz EIRP is ≈ **+51 dBm**.

**One constraint decides the shape of the answer:** each of the 16 elements has its own PA and
its own antenna, so there is no single point after a combiner to filter. It is **16 filters or
none**.

## Interface (already present)

The PA board exposes its RF in and out on SMA launches — `J1` (input) and `J2` (output). A
per-element filter therefore needs **no board change at all**: it is an inline SMA-to-SMA part
between `J2` and the antenna feed. This is the same pattern the main board's TX/RX filter
interface uses (issue #2).

## Specification

| Parameter | Value | Basis |
|---|---|---|
| Passband | 10.485 – 10.515 GHz | the 30 MHz chirp at the IF plan's LO |
| Insertion loss, passband | ≤ 1.0 dB | budget: the array's EIRP is already marginal |
| Rejection at 21.0 GHz (2nd harmonic) | **≥ 35 dB** | `aeris10_bpf_spec.md` |
| Rejection at 31.5 GHz (3rd harmonic) | **≥ 35 dB** | `aeris10_bpf_spec.md` |
| Rejection, 10.7 – 12.75 GHz | ≥ 40 dB | out-of-band interference and LO leakage |
| Impedance | 50 Ω | system impedance |
| Power handling | ≥ +40 dBm | the PA's rated output |
| Mechanical | SMA female/female inline, one per element × 16 | `J1`/`J2` are SMA |
| Group delay variation in band | ≤ 5 ns pk-pk | it sits inside the range measurement |

## What must be measured before the filter is ordered

The −20 dBc harmonic figure in the checklist is an **assumption, not a measurement**. The
filter's required order follows from the real number:

1. Drive one PA board at the intended bias and duty cycle, with the antenna port terminated.
2. Measure the 2nd and 3rd harmonic levels relative to the fundamental (a spectrum analyser to
   40 GHz, or a harmonic mixer).
3. Required stopband attenuation = (measured harmonic level) + (the margin the EIRP limit needs).

If the measurement comes back better than −20 dBc, the filter can be relaxed — possibly to a
single stub or to relying on the antenna's own roll-off, which is option C below.

## Options

| Option | Cost | Assessment |
|---|---|---|
| **A. External SMA filter per element** | 16 × filter | **Recommended.** Uses the existing `J2` interface, no board change, and the part can be specified against the measurement rather than a guess. |
| B. Microstrip low-pass on each PA board | design time + board area | Now feasible — the PA board is moving to RO4350B (see `FAB_NOTES.md`) — but a 10.5 GHz stub filter needs electromagnetic simulation to be trustworthy, and this repo has no EM tooling or measured substrate data. It should not be laid out on the strength of a hand calculation. |
| C. Accept and document | 0 | Only defensible with a measurement showing the antenna's own roll-off plus the PA's actual harmonics meet the limit. The +51 dBm unfiltered figure is what it has to beat. |

## Where this lands in the assembly

The harness/assembly drawing (`Board_Artifacts/harness_map.csv`) covers the DC interconnects;
this filter is an RF in-line item, one per element, between the PA board's `J2` and the
radiating element. It should appear in the assembly BOM as a quantity of 16 with the
measurement above as its acceptance test.
