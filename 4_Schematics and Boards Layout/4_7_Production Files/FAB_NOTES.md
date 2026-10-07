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
| Power amplifier | `RF_PA.brd` | **4** (1, 2, 15, 16) | **Rogers RO4350B, h = 0.102 mm** (decided; see below) | carries the 10.5 GHz RF in/out and the 22 V drain network |
| Power board | `PowerBoard.brd` | **2** (1, 16) | FR-4 | DC only |
| Patch antenna array | (gerbers only) | 4 | **to be re-specified on any redraw** (see below) | the radiating surface itself is at 10.5 GHz |

**Decided: the PA board moves to RO4350B.** Both RF boards were carrying 10.5 GHz with no
laminate callout. FR-4's loss tangent is ~0.02 at 10 GHz against RO4350B's ~0.0037, and — more
important for a 16-element array — FR-4's Dk varies meaningfully lot to lot, which lands
directly in the element-to-element phase calibration budget. The PA board is 4 layers at
60 × 35 mm, so the material delta is small against the array it feeds.

The PA board therefore uses the same stackup as the main board, and the controlled-impedance
section below applies to its RF traces as well. Its RF geometry should be re-solved for the
0.102 mm core rather than copied from the main board, because the trace widths depend on the
layer's distance to the reference plane.

**The patch array cannot be changed the same way**: it ships gerbers only, with no `.brd`, so
it cannot be re-spun from this repo at all. Any redraw must carry the laminate callout, and
until then its material is whatever those gerbers were cut on. That is recorded here rather
than silently inherited.

## 2. Controlled impedance (RF boards: main board and PA board)

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

## 5. Thermal path for the PA board (issue #5)

The requirement, from the array's own numbers: 16 devices dissipating 592 W into a 40 °C ambient with
an 85 °C case ceiling is **1.22 °C/W per device** case-to-ambient, or **0.076 °C/W for the array**.

**What the board provides:** 128 vias at 0.15 mm drill, of which ~104 sit in the densest cluster. A
0.15 mm plated via through 1.6 mm FR-4 is commonly taken at 80–120 °C/W, so the field is worth
**0.77–1.15 °C/W per device** — which **meets the requirement with no margin**.

So the board path is adequate and nothing about it needs changing. What it does *not* do is leave room
for the thermal interface and the heatsink, and that is what the issue's mechanical items are for.
They belong in the PO, not in the layout:

- **A pedestal to the chassis** under each PA board, so the heat leaves through metal rather than
  through the board alone. This is the cheapest way to buy back the margin.
- **A copper coin under the device paddle** if the pedestal route is not taken, or if the acceptance
  soak shows the margin has gone. It is a fab option on the PA stackup (Rogers RO4350B, per #24) and
  must be in the PO — it cannot be added after the boards are made.

**How this is verified:** the acceptance soak at 10 % duty with a baseplate thermistor. The eight
thermistor channels and the firmware's 75 °C limit are already in place — see
`docs/temperature-channels.md` and `docs/bring-up-acceptance.md`.

**On the estimate above:** the per-via figure is a common rule of thumb for a 0.15 mm plated via, not
a measurement, and the count is from the board file rather than a thermal simulation. It is stated
with its basis for that reason — **the acceptance soak is what settles it**, and the note exists so
the soak has something to be compared against.
