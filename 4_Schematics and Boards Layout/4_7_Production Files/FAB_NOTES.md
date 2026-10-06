# Fabrication and assembly notes (per board)

**Why this file exists.** The repo carries one good document — `PCBWay_Impedance_Note_RO4350B_h0p102mm.pdf`
— and no per-board statement of material, stackup, impedance or assembly requirements. A fab
or assembly PO that does not carry those requirements explicitly is a PO that will be built to
defaults. This file is the callout to attach to the PO; the PDF stays as the detailed
impedance note it already is.

Nothing here is new engineering: the numbers are read off the existing note and the board
files, so that the PO and the design cannot drift apart.

## 1. Boards, layers and material

Copper layer counts are read from each `.brd` (layer numbers ≤ 16), and match the quotations
obtained earlier from PCBWay.

| Board | File | Copper layers | Material callout | Notes |
|---|---|---|---|---|
| Main board | `RADAR_Main_Board.brd` | **10** (1–5, 12–16) | **Rogers RO4350B**, h = 0.102 mm | the RF board: carries the 10.5 GHz TX/RX paths, the ADC LVDS and the FPGA |
| Frequency synthesizer | `Clocks_Freq_Synth_board.brd` | **6** (1–3, 14–16) | FR-4 unless a controlled-impedance run is required | clocks to 3.6 GHz internal, 300/400 MHz outputs; LVDS pairs benefit from impedance control |
| Power amplifier | `RF_PA.brd` | **4** (1, 2, 15, 16) | **FR-4 as drawn — see the open question below** | carries the 10.5 GHz RF in/out and the 22 V drain network |
| Power board | `PowerBoard.brd` | **2** (1, 16) | FR-4 | DC only |
| Patch antenna array | (gerbers only) | 4 | FR-4 as drawn — see the open question below | the radiating surface itself is at 10.5 GHz |

**Open question that this file surfaces rather than answers:** the PA board and the patch
array both handle 10.5 GHz, and neither has a laminate callout. On FR-4 (tan δ ≈ 0.02 at
10 GHz, and Dk that varies ±0.2 lot to lot) the RF insertion loss and the phase repeatability
between the 16 elements are both worse than on RO4350B. This was raised as an observation, not
a measured claim — but the decision to keep those two boards on FR-4 should be a decision, and
the array's phase calibration budget is where it would show up.

## 2. Controlled impedance (RF boards)

From `PCBWay_Impedance_Note_RO4350B_h0p102mm.pdf`:

* Material RO4350B, dielectric thickness **h = 0.102 mm**;
* design Dk **3.66**, process Dk **3.48 ± 0.05** (IPC clamped);
* copper 35 µm base, ~40–45 µm finished;
* targets: **Z0 = 50 Ω ± 10 %** single-ended, **Zdiff = 100 Ω ± 8 %** differential;
* starting geometry (maskless microstrip): w = **0.204 mm**, diff-pair gap s = 0.26 mm.

**The design agrees with the note**: the main board's dominant top-layer trace width is
**0.204 mm** (2591 segments), with 0.2 mm on a further 2079 — a 2 % difference, inside the
±10 % target. Two widths coexist, so the PO should state a tolerance that covers both rather
than a single nominal.

## 3. Requirements to put in the PO (all boards)

1. **Coupon / TDR report is a deliverable.** The note already says the geometries are
   "starting points for PCBWay CAM to field-solve and tune with coupons" — make the coupon
   report part of the deliverable, not an option.
2. **Maskless RF traces** on the laminate boards (the note: "solder mask: none, maskless RF
   traces over air"). A mask over the RF layer changes the effective Dk and therefore the
   impedance; state it explicitly.
3. **Continuous reference plane** beneath the RF and LVDS routing — no splits or slots under
   a pair (the note's routing rule).
4. **Stackup drawing in the PO**, matched to the impedance note's h = 0.102 mm; a fab that
   substitutes a different prepreg thickness invalidates the geometry.
5. **Assembly**: `BOM_*.xlsx` + `CPL.xlsx` per board are the authoritative pair (both
   generated from the `.brd`), and `Board_Artifacts/DNP_list.csv` states what must not be
   fitted. Note that the PA board's `BOM.xlsx` and `BOM_PA.xlsx` both exist — the PO must name
   which one is authoritative.

## 4. Known deviations that the PO should not silently accept

* The patch-array board ships **gerbers only** (no `.brd`), so it cannot be re-spun from this
  repo without redrawing it; any change to the array is a new design, not an edit.
* `9_Firmware/9_2_FPGA` targets **XC7A100T** while the BOM lists the XC7A50T — the fab/assembly
  must be told which part is stuffed (the 50T cannot hold the integrated firmware).
