# AERIS-10 bring-up checklist (measured gates, not vibes)

Every gate below states **what to measure**, **what it must read**, and **what to do if it does not**.
Nothing here needs the full array: gates 1–4 run on a *bench slice* (one ADAR1000 + four ADTR1107,
no antenna array), which covers most of the electrical risk before the 16-PA spend.

> **Before you power anything:** on the current revision the two RF band-pass filters (`U$2`/`U$3`,
> device `BPF2`) are marked *"Not a component / Do not put"* in the production BOM
> (`bom/DNP_list.csv`). A board built exactly as the files specify has **no TX/RX filtering at all**.
> For the bench slice, fit 0 Ω bridges (or real filters) at those footprints deliberately — do not
> assume a filter is present because the schematic draws one.

Instruments: bench PSU with current limit, 4-channel scope (≥1 GHz), DMM, VNA (gate 4 only), thermocouple.

---

## Gate 0 — pre-power inspection (no power applied)

| Check | Pass condition |
|---|---|
| Rail continuity between boards | Every rail in `bom/interboard_rails.csv` reads < 0.5 Ω end-to-end. **Watch the near-duplicate names**: `+3V3_ADAR12` on the main board is routed as `+3V3_ADAR_12` on the power board — confirm they are the same net, not two nets |
| Populated vs `bom/DNP_list.csv` | All 22 DNP parts (including `U$2`,`U$3`,`SJ1`,`SV1` and the 18 jumper headers) are **left off**, or explicitly documented as fitted for a reason |
| Shunt value at `R10` | Measure it. The board silkscreen value says `5mR`; the BOM MPN `WSL2816R1000FEH` is **0.1 Ω**. Fit what the sense chain expects (5 mΩ) or change `PA_SHUNT_OHMS` in `PA_SENSE.h` to match what is fitted |
| ADC bank rail | `VCCO_14` must be **2.5 V** for the LVDS ADC lanes. The current schematic ties it to `+3V3_FPGA`; if it reads 3.3 V, do not proceed to gate 1 until the rail is fixed or the ADC lanes are re-thought |

## Gate 1 — rails, sequencing, clocks (no PA bias)

| Measurement | Target | If it fails |
|---|---|---|
| Each rail at no load, in the order the power board brings them up | ±5 % of nameplate; `−5V5_PA` must not sag below −5.2 V with the OPA4703 loads attached | `−5V5_PA` sits at 100 % of its LM2662 (200 mA) — if it sags, the negative pump needs a second device or the OPA supply must move onto the under-used −5 V ADAR rails |
| `+3V3_ADAR_12/34` at full ADAR load | ≥ 2.9 V (these two 2 A bucks run at ~35 % load) | check the feedback divider, not the load |
| ADC DCO | **400 MHz** ±50 ppm at `adc_dco_p` | clock plan/config error in the AD9523 or MMCM, not the ADC |
| `adc_pwdn` low, ADC out of power-down | ADC draws its rated current | strap/GPIO conflict |
| No PGOOD/FAULT nets exist on this revision | Assert rail state by **measurement**, and see `DIG_7` below for fault signalling | — |

## Gate 2 — FPGA/ADC link integrity

| Measurement | Target | If it fails |
|---|---|---|
| Vivado DRC | **zero waivers** except a documented `PLIO-9` — the `BIVC-1` waiver exists only because `VCCO_14` is 3.3 V instead of 2.5 V. With the 2.5 V rail present, build `scripts/100t/build_100t.tcl` and **remove the waiver** | a build that needs `BIVC-1` waived is telling you the bank rail is wrong |
| Half-scale DC into the ADC, capture | No bit errors over 10⁶ samples; LVDS eye open | reduce clock rate and re-check termination; do not "fix" it in firmware |
| FPGA self-test | Passes (the `System Top` and `Self-Test` suites in `run_regression.sh` are the same logic in simulation) | — |
| `DIG_5` / `DIG_7` | Drive a known strong input: `DIG_5` sets on any clipped sample **or on a real ADC analog overrange** (the AD9484 `OR` pin, balls M6/N6, now constrained); `DIG_7` sets only when **≥ 8** samples clip in a frame or the ADC reports overrange | if `DIG_7` is stuck low, the RTL assign is not in the bitstream you loaded |
| `DIG_5` with the AGC backed off | Reduce the analog level until the digital clip count is zero, then inject enough RF to trip the ADC's `OR`: `DIG_5` must still assert. This is the case the digital clip count alone cannot see | if it does not, the `OR` pair (M6/N6) is not reaching the receiver — check the IBUFDS in `ad9484_interface_400m.v`, not the AGC |
| `DIG_7` idle | Low with no signal, and not floating | — |

## Gate 3 — PA bias and protection (one PA board, current-limited PSU)

| Measurement | Target | If it fails |
|---|---|---|
| Gate voltage with the DAC in reset, before VD is applied | VGG ≈ −4 V, IDQ ≈ 0 | never apply VD to a FET whose gate bias is unknown |
| Calibrated IDQ, all 16 channels | **1.680 A ± 2 %** (≈ code 43 of 255 on the ADS7830 with the 5 mΩ/50 V/V chain) | if the reported current is ~32 % high, the ADC reference constant is wrong for how the ADS7830 was initialised (internal 2.5 V vs 3.3 V) |
| Injected current check | Inject 1 A and 2 A through the shunt; firmware reads within ±2 % | calibrate/replace the sense chain, not the numbers in the log |
| Over-current trip | Trips between **2.5 A and 10 A**; the chain must be able to see the trip current at all (a 0.1 Ω shunt saturates at 0.5 A and can *never* trip) | `PA_SENSE.h`'s `static_assert` exists precisely to make that combination fail the build |
| Bias fault detection | Reads < 0.1 A → `BIAS FAULT` reported | — |
| VGA write verification | Zero failed read-backs out of 16 (`applyGainVerified`) | a non-zero count means the ADAR SPI path is unreliable — stop and fix the bus |

## Gate 4 — IF chain and filters (VNA + noise figure)

| Measurement | Target | If it fails |
|---|---|---|
| Cascade gain, mixer → ADC | **≈ 30 dB**. Built as-is: `U4` (R14 = 115 Ω = 10 dB) + `U8` (R22 = 56 Ω = 15 dB) = **25 dB**, i.e. ~5 dB short | change `R14` 115 Ω → 56 Ω (10 → 15 dB) for 30 dB and re-measure |
| ADC input level with a known target | ≈ −40 dBFS wideband noise per the design's own target | use the IF gain trim, not a firmware fudge |
| TX/RX filter IL | TX ≤ 1.5 dB, RX ≤ 1.0 dB | re-run the link budget with the measured IL |
| Image rejection | **≥ 40 dB** at the image (240 MHz offset, low-side LO 10.38 GHz) | the filter is a placeholder until a real one is fitted |
| Filter group delay | ≤ 5 ns pk-pk across the passband | reduce filter order or widen the passband |
| RX noise figure at the antenna port | ≤ 3.5 dB with the real filter | revisit the LNA/filter order |

## Gate 5 — one PA + one antenna column (RF + thermal)

| Measurement | Target | If it fails |
|---|---|---|
| EIRP per column | Matches the link budget used for the range claim | check the harmonic filter IL and the gate bias |
| Harmonics | 2nd/3rd ≥ 20 dB below carrier at the antenna port — **no post-PA filter exists on this revision** | add the harmonic filter before quoting EIRP |
| VD droop during a chirp burst | **< 0.5 V** (30 µs × 1.68 A needs ≥ 100 µF per board; the current 30 µF droops ~1.7 V) | add bulk capacitance at the drain feed |
| Baseplate temperature, 30-min soak at 10 % duty | **≤ 85 °C** with margin | the thermal path is not solved: copper coin/pedestal to the chassis, thermistor on the baseplate |

## Gate 6 — full array

| Measurement | Target | If it fails |
|---|---|---|
| Beam steering vs the channel map | Per-element response matches `ADAR1 → FE 3,1,2,4`, `ADAR2 → 7,5,6,8`, `ADAR3 → 10,12,11,9`, `ADAR4 → 14,16,15,13` | the element↔channel order is the one place a swapped ribbon cable looks like a "beam pointing" bug |
| AGC recovery | After a forced overload, gain recovers within **≈ 4 s** (30 codes at 2 codes/frame, 258 ms frames). The firmware prints this figure at boot — if the log says ~31 s, the old asymmetric defaults are still in the build | check `gain_step_up`/`holdoff_frames` and that the boot log line is present |
| AGC attack magnitude | A hard overload (`DIG_7`) steps 8 codes; a marginal clip steps 4 | confirm `DIG_7` reaches PD15 |
| Sidelobes / pattern | Within the array design target; ±45° within ~2–3 dB scan loss (characterised for the patch array only) | — |
| T/R switching | Clean, no receive-window leakage | — |
| End-to-end detection | A known RCS target at a known range, with the RCS and dwell recorded next to the result | quote range only with its RCS and dwell (see the README's basis section) |

---

## Repeatable checks (run these instead of eyeballing)

```bash
python3 9_Firmware/tools/regenerate_bom.py . --check     # BOM vs board drift + accepted baseline
python3 9_Firmware/tools/extract_dnp.py . --check        # the single DNP list is current
python3 9_Firmware/tools/interboard_rails.py . --check   # rail map + near-duplicate names
cd 9_Firmware/9_1_Microcontroller/tests && make test      # MCU unit tests (incl. AGC + sense chain)
cd 9_Firmware/9_2_FPGA && bash run_regression.sh          # 27 RTL suites
```
