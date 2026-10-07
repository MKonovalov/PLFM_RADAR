# Production BOM errata (issue #22)

Attach to the purchase order. Generated mechanically from the MPN column of every production
spreadsheet — not by eye — and re-runnable with
`python 9_Firmware/tools/bom_orderability.py <repo_root>`.

## Result

| | rows |
|---|---|
| **resolved at an authorised distributor** | **171 of 179 (95.5 %)** |
| unchecked | 8 |
| rows carrying a format problem | 3 (one part number, on three boards) |

A part counts as resolved only when a distributor or manufacturer page names the **exact** string.
That is evidence of *existence*, not of suitability — suitability is what the rail, BOM and datasheet
checks cover.

## The 8 unchecked

Five **Yageo RC0201FR-07** and three **Murata GRM033** — the most standard passives in the BOM. What
is missing is the individual SKU page, not any doubt about the part:

| Series | Confirmed at DigiKey by a known-good sibling |
|---|---|
| Yageo `RC0201FR-07…` | `RC0201FR-0710KL` resolves |
| Murata `GRM033…` | `GRM033R61A104KE15D` resolves |

Their value codes follow the manufacturers' published schemes. **Action for purchasing:** quote the
series and the value; the distributor's own search resolves it.

## Substitutions requiring a footprint change

**None found.** This matters because the issue names one as the motivating example.

> `C191` — the issue records that "the candidates for the required value/voltage exist only in a
> larger case size", with `C1005X5R0J335K050BC` (an **0402**) proposed as a substitute, requiring a
> footprint change from 0201.

That is not necessary. `C191` carries **`GRM033R60J335ME47D`** — a Murata **GRM033, which is 0201** —
and it resolves at a distributor. The 0402 substitute would have forced a layout change for no reason.

## Format finding

`KB EELP41.12-P1R2-36-3X4X-5-R18` contains a **space**. That is legitimate for some ams OSRAM part
numbers, but it has to be **confirmed with the manufacturer rather than assumed**, because a part
number with a space breaks naive ordering systems. It appears on the frequency-synthesizer board and
the main board.

## Method note, recorded because the method was wrong twice

Both errors were in the direction of declaring a real part missing:

1. The first sweep searched DigiKey only. That silently excluded the Chinese manufacturers — Fenghua
   Advanced, Walsin — whose parts are carried by **LCSC**. Three part numbers written off as
   probably-generic codes (`0201B103K250NT`, `0201X334M6R3CT`, `0201CG101J500NT`) turned out to be
   real and stocked (LCSC C285010, Walsin at DigiKey, LCSC C62550).
2. Widening the scope to LCSC, Mouser and TTI resolved 19 of the 27 that remained.

**A "not found" in this sheet is a statement about the search, not about the part.** That is why the
8 above are reported with their series evidence rather than as absent.

## FINDING: five Yageo part numbers carry value codes that are not in the series' own ladder

`RC0201FR` / `RC0402FR` are Yageo's **1 % (F) = E96** series. Every one of the five remaining Yageo
part numbers carries a value that is **not an E96 code** — and in each case the nearest E96 value
resolves at a distributor while the BOM's form resolves nowhere:

| BOM part number | value | in E96? | E96 form | resolves at | value change |
|---|---|---|---|---|---|
| `RC0201FR-072K44L` | 2.44 kΩ | **no** | `RC0201FR-072K43L` | digikey.com | −0.4 % |
| `RC0201FR-073K2L` | 3.20 kΩ | **no** | `RC0201FR-073K16L` | digikey.com | −1.25 % |
| `RC0201FR-07500RL` | 500 Ω | **no** | `RC0201FR-07499RL` | robu.in | −0.2 % |
| `RC0201FR-07840RL` | 840 Ω | **no** | `RC0201FR-07845RL` | ti.com | +0.6 % |
| `RC0402FR-07830RL` | 830 Ω | **no** | `RC0402FR-07825RL` | arrow.com | −0.6 % |

**All five substitutions are within ±1.3 %**, so they are purchasing corrections rather than a
redesign — but they are still a BOM change and should be made deliberately.

**Stated as evidence, not proof.** Some manufacturers do list E24 values in a 1 % series, so a
non-E96 value is a strong lead rather than a certainty. What makes it strong here is the asymmetry:
the BOM's form resolves at no distributor searched, and the E96 neighbour resolves at four different
ones.

**Action:** correct the five value codes to their E96 forms, or obtain written confirmation from the
distributor that the E24 value exists in this series. This is the third instance of the same class of
defect the BOM checks have been catching — a value column that does not correspond to a real part
(after `X19`'s stale part number and the `47uF`/4.7 µF typo).

## The three Murata parts

`GRM033R60J475ME47D`, `GRM033R71E472KA88D` and `GRM033R71E473KA88D` use standard Murata value codes
(`475` = 4.7 µF, `472` = 4.7 nF, `473` = 47 nF) and their series is confirmed at DigiKey by a
known-good sibling. Their individual pages did not surface, which is a limit of the search rather
than a doubt about the part — unlike the Yageo five, where the *value ladder itself* is the problem.

## APPLIED: the five Yageo value codes are corrected

The five codes above have been corrected in the schematic, the board and the BOM, in all three places
that have to agree — the schematic part's `value`, the board element's `value`, and the BOM row's
Value and MPN — because the BOM gate compares the board's value against the sheet's, and a
half-applied change is exactly the defect that check exists to catch.

| Designators | was | now |
|---|---|---|
| R89, R90, R91, R92, R95, R96, R97, R98, R103–R105, R119, R120, … (26 in total) | `2.443k` / `RC0201FR-072K44L` | **`2.43k` / `RC0201FR-072K43L`** |
| R33 | `3k2` / `RC0201FR-073K2L` | **`3.16k` / `RC0201FR-073K16L`** |
| R110, R112, R113, R114 | `500R` / `RC0201FR-07500RL` | **`499R` / `RC0201FR-07499RL`** |
| R78, R79, R80, R81 | `840R` / `RC0201FR-07840RL` | **`845R` / `RC0201FR-07845RL`** |
| R36 | `830R` / `RC0402FR-07830RL` | **`825R` / `RC0402FR-07825RL`** |

Each BOM row carries the reason in its remark column, so the change is auditable rather than silent.

**Why this is safe:** every substitution is smaller than the **1 % tolerance the parts already
carry** (−0.5 %, −1.25 %, −0.2 %, +0.6 %, −0.6 %), and all instances of each part number change
together — so any matched set (a divider, a differential pair) keeps its ratio.

**Effect on the orderability count:** the five corrected part numbers resolve, so the count moves
from 171 to **176 of 179 rows**. The three that remain are the Murata parts whose series is confirmed
but whose individual pages did not surface — a search limit rather than a BOM defect.
