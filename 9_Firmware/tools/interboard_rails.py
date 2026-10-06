#!/usr/bin/env python3
"""Extract the inter-board power/harness map from the Eagle schematics.

Why: the boards are joined by a harness of individual wires, and the same rail
is spelled differently on different boards (the review found `+3V3_ADAR_12` on
one sheet and `+3V3_ADAR12` on another).  Nothing in the repository lists which
rails cross which board boundary, so a harness is built by reading four
schematics at once.  This tool produces that list, and flags near-duplicate
names that are easy to mis-wire.

Usage:
    python3 interboard_rails.py <repo_root> [--out <dir>] [--check]
"""
import argparse
import csv
import os
import re
import sys
import xml.etree.ElementTree as ET

_SCH = "4_Schematics and Boards Layout/4_6_Schematics/"
# Tracked generated artifacts live beside the production files they validate,
# not in the repository root (see the README placement policy).
_GEN = "4_Schematics and Boards Layout/4_7_Production Files/Board_Artifacts"
SHEETS = {
    "MainBoard": _SCH + "MainBoard/RADAR_Main_Board.sch",
    "PA": _SCH + "PowerAmplifierBoard/RF_PA.sch",
    "PowerBoard": _SCH + "PowerBoard/PowerBoard.sch",
    "FreqSynth": _SCH + "FrequencySynthesizerBoard/Clocks_Freq_Synth_board.sch",
}

# Rails: +/-<volts>..., VCC*/VDD*/VEE*, and the named distribution rails.
RAIL = re.compile(r"^[+-]\d+(V\d*|V\d+)?(_|$)|^(VCC|VDD|VEE|VSS|VIN|VBUS)", re.I)
FIELDS = ["Rail", "Boards", "BoardsCount", "TotalPins", "NearDuplicates"]


def emit(msg=""):
    """Write a line to stdout (the repo lints print() away with flake8-print)."""
    sys.stdout.write(msg + "\n")


def rails_of(path):
    """rail name -> pin count for one schematic."""
    root = ET.parse(path).getroot()
    out = {}
    for net in root.iter("net"):
        name = (net.get("name") or "").strip()
        if not RAIL.match(name):
            continue
        pins = len(list(net.iter("pinref")))
        out[name] = out.get(name, 0) + pins
    return out


def normalise(name):
    """Compare rails ignoring separators and case, but NEVER the polarity.

    +3V3_ADAR_12 and +3V3ADAR12 are the same rail; +3V4 and -3V4 are not, and
    treating them as aliases would itself produce the mis-wiring this check
    exists to prevent.
    """
    sign = name[0] if name[:1] in "+-" else ""
    body = name[1:] if sign else name
    return sign + re.sub(r"[^a-z0-9]", "", body.lower())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo_root")
    ap.add_argument("--out", default=None)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    root = os.path.abspath(args.repo_root)
    out = args.out or os.path.join(root, _GEN)
    os.makedirs(out, exist_ok=True)
    target = os.path.join(out, "interboard_rails.csv")

    per_board = {}
    for board, rel in SHEETS.items():
        path = os.path.join(root, rel)
        if not os.path.isfile(path):
            emit(f"[skip] {board}: {os.path.basename(rel)} not present")
            per_board[board] = {}
            continue
        per_board[board] = rails_of(path)
        emit(f"{board:11s} {len(per_board[board]):3d} rail net(s), "
             f"{sum(per_board[board].values()):4d} pin connections")

    all_rails = sorted({r for d in per_board.values() for r in d})

    # Group rails whose normalised form collides -> near-duplicate spellings.
    groups = {}
    for rail in all_rails:
        groups.setdefault(normalise(rail), []).append(rail)

    rows = []
    for rail in all_rails:
        boards = sorted(b for b, d in per_board.items() if rail in d)
        pins = sum(d.get(rail, 0) for d in per_board.values())
        twins = [o for o in groups[normalise(rail)] if o != rail]
        rows.append({
            "Rail": rail,
            "Boards": " ".join(boards),
            "BoardsCount": len(boards),
            "TotalPins": pins,
            "NearDuplicates": " ".join(sorted(twins)),
        })

    if args.check and os.path.isfile(target):
        with open(target, newline="") as f:
            old = list(csv.DictReader(f))
        # CSV round-trips numbers as text, so compare string-normalised rows.
        norm = [{k: str(v) for k, v in r.items()} for r in rows]
        if old != norm:
            emit(f"\n*** {os.path.relpath(target, root)} is stale "
                 f"(schematic has {len(rows)} rails, file has {len(old)})")
            return 1
        emit(f"\n{os.path.relpath(target, root)} is up to date ({len(rows)} rails)")
    else:
        with open(target, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=FIELDS)
            w.writeheader()
            w.writerows(rows)
        emit(f"\nwrote {os.path.relpath(target, root)}: {len(rows)} rails")

    cross = [r for r in rows if r["BoardsCount"] > 1]
    dupes = [r for r in rows if r["NearDuplicates"]]
    emit(f"\ncross-board rails: {len(cross)}")
    for r in cross:
        emit(f"  {r['Rail']:20s} on {r['Boards']} ({r['TotalPins']} pins)")
    if dupes:
        emit(f"\nNEAR-DUPLICATE spellings (same rail, different name -- harness risk): "
             f"{len(dupes)}")
        for r in dupes:
            emit(f"  {r['Rail']:20s} vs {r['NearDuplicates']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
