#!/usr/bin/env python3
"""Extract the consolidated Do-Not-Populate (DNP) list from the production BOMs.

Why: the DNP markers live only inside per-board spreadsheets, as free text in a
notes column ("Do Not Put", "[DNP不要贴]", "Not a component").  Nothing in the
repository states which parts must be left off, so an assembly house is asked to
infer it from prose -- and the two RF band-pass filters (U$2/U$3, "BPF2") are
among the parts marked "Not a component / Do not put", which is why a build from
these files has no TX/RX filtering at all (feasibility review P0 blocker 1).

This tool turns that prose into one list that can be handed to a fab and checked
in CI.

Usage:
    python3 extract_dnp.py <repo_root> [--out <dir>] [--check]

    --check   exit non-zero if the committed list differs from the sheets
"""
import argparse
import csv
import os
import re
import sys
import xml.etree.ElementTree as ET

_PROD = "4_Schematics and Boards Layout/4_7_Production Files/"
# Tracked generated artifacts live beside the production files they
# validate, not in the repository root (see the README placement policy).
_GEN = "4_Schematics and Boards Layout/4_7_Production Files/Board_Artifacts"

SHEETS = {
    "MainBoard": _PROD + "Gerber_Main_Board/BOM_Main_Board.xlsx",
    "PA": _PROD + "Gerber_PA/BOM_PA.xlsx",
    "PowerBoard": _PROD + "Gerber_PowerBoard/BOM_Power_Board.xlsx",
    "FreqSynth": _PROD + "Gerber_freq_synth/BOM_Freq_Synth.xlsx",
    "PatchAntenna": _PROD + "Gerber_Patch_Antenna/BOM_Patch_Antenna.xlsx",
}

# Board files used to attach a package/description to each designator where the
# board file is available (the patch antenna ships gerbers only).
BOARDS = {
    "MainBoard": "4_Schematics and Boards Layout/4_6_Schematics/MainBoard/RADAR_Main_Board.brd",
    "PA": "4_Schematics and Boards Layout/4_6_Schematics/PowerAmplifierBoard/RF_PA.brd",
    "PowerBoard": "4_Schematics and Boards Layout/4_6_Schematics/PowerBoard/PowerBoard.brd",
    "FreqSynth": "4_Schematics and Boards Layout/4_6_Schematics/FrequencySynthesizerBoard/"
                 "Clocks_Freq_Synth_board.brd",
}

MARKERS = ("dnp", "不要贴", "not a component", "do not put", "do not populate", "nopop")
REFDES = re.compile(r"^[A-Za-z$]{1,3}\$?\d{1,4}[A-Za-z]?$")
FIELDS = ["Board", "Designator", "Package", "Value", "Marker"]


def emit(msg=""):
    """Write a line to stdout (the repo lints print() away with flake8-print)."""
    sys.stdout.write(msg + "\n")


def board_parts(path):
    """designator -> (package, value) from an Eagle board file."""
    if not path or not os.path.isfile(path):
        return {}
    out = {}
    for el in ET.parse(path).getroot().iter("element"):
        out[el.get("name")] = (el.get("package") or "", el.get("value") or "")
    return out


def dnp_rows(board, sheet_path, parts):
    """Rows flagged DNP in one BOM sheet."""
    import openpyxl

    rows = []
    ws = openpyxl.load_workbook(sheet_path, data_only=True).active
    for row in ws.iter_rows(values_only=True):
        cells = ["" if c is None else str(c).strip() for c in row]
        marker_idx = next((i for i, c in enumerate(cells)
                           if any(m in c.lower() for m in MARKERS)), None)
        if marker_idx is None:
            continue
        marker = cells[marker_idx]
        # The designator cell is the LAST refdes-looking cell before the notes
        # column: device/package columns can look like refdes too (a row whose
        # device is "BPF2" or "SJ2W" would otherwise be picked over "U$2, U$3").
        best = None
        for cell in cells[:marker_idx]:
            refs = [r.strip() for r in re.split(r"[,\s;]+", cell) if r.strip()]
            good = [r for r in refs if REFDES.match(r)]
            if good and len(good) >= len(refs):
                best = good
        if not best:
            continue
        for ref in best:
            package, value = parts.get(ref, ("", ""))
            rows.append({
                "Board": board, "Designator": ref,
                "Package": package, "Value": value, "Marker": marker,
            })
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo_root")
    ap.add_argument("--out", default=None)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    root = os.path.abspath(args.repo_root)
    out = args.out or os.path.join(root, _GEN)
    os.makedirs(out, exist_ok=True)
    target = os.path.join(out, "DNP_list.csv")

    all_rows = []
    for board, sheet_rel in SHEETS.items():
        sheet = os.path.join(root, sheet_rel)
        if not os.path.exists(sheet):
            emit(f"[skip] {board}: {os.path.basename(sheet_rel)} not present")
            continue
        parts = board_parts(os.path.join(root, BOARDS.get(board, "")))
        rows = dnp_rows(board, sheet, parts)
        all_rows.extend(rows)
        emit(f"{board:13s} {len(rows):3d} DNP part(s)")

    all_rows.sort(key=lambda r: (r["Board"], r["Designator"]))

    if args.check and os.path.exists(target):
        with open(target, newline="") as f:
            old = list(csv.DictReader(f))
        if old != all_rows:
            emit(f"\n*** {os.path.relpath(target, root)} is stale: "
                 f"sheet has {len(all_rows)} rows, file has {len(old)}")
            emit("    re-run without --check to regenerate it")
            return 1
        emit(f"\n{os.path.relpath(target, root)} is up to date "
             f"({len(all_rows)} DNP parts)")
        return 0

    with open(target, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        w.writeheader()
        w.writerows(all_rows)

    emit(f"\nwrote {os.path.relpath(target, root)}: {len(all_rows)} DNP parts")
    by_board = {}
    for r in all_rows:
        by_board.setdefault(r["Board"], []).append(r["Designator"])
    for board, refs in by_board.items():
        emit(f"  {board}: {' '.join(refs)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
