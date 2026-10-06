#!/usr/bin/env python3
"""Regenerate the production BOM/CPL from the Eagle board files.

Why: the published BOM spreadsheets in 4_7_Production Files were transcribed by
hand and have drifted from the boards.  The feasibility review found parts that
exist only in the BOM (a 0.1 ohm shunt where the value column said 5 mR),
non-existent capacitor part numbers, and a 16-vs-11 SMA-jack count difference on
the patch-array board.  A BOM that cannot be generated cannot be trusted.

This tool is the single source of generation: it reads the .brd XML (the board
file is the authority for what is placed), emits BOM + CPL CSVs, and diffs them
against the committed spreadsheet so every difference is either explained or
fixed.

Usage:
    python3 regenerate_bom.py <repo_root> [--out <dir>] [--check]

    --check   exit non-zero if the committed spreadsheet disagrees with the board
"""
import argparse
import csv
import os
import re
import sys
import xml.etree.ElementTree as ET

_SCH = "4_Schematics and Boards Layout/4_6_Schematics/"
_PROD = "4_Schematics and Boards Layout/4_7_Production Files/"
# Tracked generated artifacts live beside the production files they
# validate, not in the repository root (see the README placement policy).
_GEN = "4_Schematics and Boards Layout/4_7_Production Files/Board_Artifacts"

BOARDS = {
    "MainBoard": _SCH + "MainBoard/RADAR_Main_Board.brd",
    "PA": _SCH + "PowerAmplifierBoard/RF_PA.brd",
    "PowerBoard": _SCH + "PowerBoard/PowerBoard.brd",
    "FreqSynth": _SCH + "FrequencySynthesizerBoard/Clocks_Freq_Synth_board.brd",
}

# Committed spreadsheets to diff against.  The patch-array board ships gerbers
# only (no .brd), so it cannot be regenerated from source and stays
# hand-maintained until an Eagle board file is committed (16-vs-11 SMA jacks).
SHEETS = {
    "MainBoard": [_PROD + "Gerber_Main_Board/BOM_Main_Board.xlsx"],
    "PA": [_PROD + "Gerber_PA/BOM_PA.xlsx"],
    "PowerBoard": [_PROD + "Gerber_PowerBoard/BOM_Power_Board.xlsx"],
    "FreqSynth": [_PROD + "Gerber_freq_synth/BOM_Freq_Synth.xlsx"],
}

BOM_FIELDS = [
    "Designator", "Value", "Device", "Deviceset", "Package", "Library",
    "PartNo", "DNP",
]
CPL_FIELDS = ["Designator", "X", "Y", "Rotation", "Side"]
SUMMARY_FIELDS = ["Qty", "Value", "Device", "Package", "MPN", "Designators"]

# Mechanical packages / libraries are named by function and legitimately carry
# no value, so they are excluded from the "unset value" finding.
MECHANICAL = ("con-ptr", "mech", "hole", "wirepad", "test")

REF_HEADERS = ("designator", "reference", "refdes", "ref", "parts", "part")
VAL_HEADERS = ("value", "values", "val")
MPN_HEADERS = ("manufacturer_part_number", "mpn", "partnumber", "part_number",
               "manufacturer pn", "manufacturer_part_no")


def emit(msg=""):
    """Write a line to stdout (the repo lints print() away with flake8-print)."""
    sys.stdout.write(msg + "\n")


def parse_board(path):
    """Return (rows, cpl) from an Eagle .brd file.

    rows: list of dicts (Designator, Value, Device, Package, ...)
    cpl : list of dicts (Designator, X, Y, Rotation, Side)
    """
    root = ET.parse(path).getroot()

    # part name -> (deviceset, device, value, attributes).  Boards saved without
    # a schematic link have no <part> section, hence the element fallbacks.
    parts = {}
    for part in root.iter("part"):
        attrs = {a.get("name"): a.get("value") for a in part.findall("attribute")}
        parts[part.get("name")] = {
            "deviceset": part.get("deviceset") or "",
            "device": part.get("device") or "",
            "value": part.get("value") or "",
            "attrs": attrs,
        }

    # (library, deviceset, device) -> package name
    pkg_of = {}
    for lib in root.iter("library"):
        libname = lib.get("name")
        for ds in lib.findall("devicesets/deviceset"):
            dsname = ds.get("name")
            for dev in ds.findall("devices/device"):
                pkg_of[(libname, dsname, dev.get("name"))] = dev.get("package") or ""

    rows, cpl = [], []
    for el in root.iter("element"):
        el_name = el.get("name")
        p = parts.get(el.get("part") or el_name, {})
        package = el.get("package") or pkg_of.get(
            (el.get("library"), p.get("deviceset"), p.get("device")), "")
        attrs = p.get("attrs", {})
        rows.append({
            "Designator": el_name,
            "Value": el.get("value") or p.get("value", ""),
            "Device": p.get("device", "") or el.get("deviceset", ""),
            "Deviceset": p.get("deviceset", "") or el.get("deviceset", ""),
            "Package": package,
            "Library": el.get("library", ""),
            "PartNo": attrs.get("MPN", attrs.get("PARTNUMBER", "")),
            "DNP": attrs.get("DNP", attrs.get("POPULATE", "")),
        })
        rot = el.get("rot", "0") or "0"
        if "R" in rot:
            rot = rot.split("R")[1] or "0"
        cpl.append({
            "Designator": el_name, "X": el.get("x", ""), "Y": el.get("y", ""),
            "Rotation": rot, "Side": el.get("side", "top"),
        })
    return rows, cpl


def write_csv(path, rows, fields):
    """Write rows to a CSV, sorted by designator."""
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        w.writeheader()
        for r in sorted(rows, key=lambda r: r["Designator"]):
            w.writerow(r)


def group_parts(rows):
    """Group placed parts into orderable line items."""
    groups = {}
    for r in rows:
        key = (r["Value"], r["Device"] or r["Deviceset"], r["Package"], r["PartNo"])
        groups.setdefault(key, []).append(r["Designator"])
    return groups


def bom_summary(groups):
    """Turn grouped parts into a qty-ordered summary."""
    out = []
    ordered = sorted(groups.items(), key=lambda kv: -len(kv[1]))
    for (value, device, package, partno), refs in ordered:
        out.append({
            "Qty": len(refs),
            "Value": value,
            "Device": device,
            "Package": package,
            "MPN": partno,
            "Designators": " ".join(refs),
        })
    return out


def read_sheet(path):
    """Read a committed BOM spreadsheet -> {designator: {"value":…, "mpn":…}}.

    The spreadsheet is the authority for orderable data (value + MPN); the board
    file is only authoritative for what is placed.  Checking placement against
    the board while ignoring the sheet's MPN column produced false "unorderable"
    findings for parts whose value/MPN exist only in the sheet.
    """
    try:
        import openpyxl
    except ImportError:
        return None
    if not os.path.exists(path):
        return None
    ws = openpyxl.load_workbook(path, data_only=True).active
    idx_ref = idx_val = idx_mpn = None
    result = {}
    for row in ws.iter_rows(values_only=True):
        cells = ["" if c is None else str(c).strip() for c in row]
        if idx_ref is None:  # still looking for the header row
            for i, c in enumerate(cells):
                lc = c.lower()
                if lc in REF_HEADERS:
                    idx_ref = i
                if lc in VAL_HEADERS:
                    idx_val = i
                if lc in MPN_HEADERS:
                    idx_mpn = i
            continue
        ref_col = idx_ref
        if ref_col is not None and ref_col < len(cells) and cells[ref_col]:
            # one cell can hold several refs ("R1 R2 R3" or "R1,R2")
            for ref in re.split(r"[,\s;]+", cells[ref_col]):
                if ref:
                    val = cells[idx_val] if idx_val is not None and idx_val < len(cells) else ""
                    mpn = cells[idx_mpn] if idx_mpn is not None and idx_mpn < len(cells) else ""
                    result[ref] = {"value": val, "mpn": mpn}
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo_root")
    ap.add_argument("--out", default=None)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--update-baseline", action="store_true",
                    help="record the current findings as the accepted baseline")
    args = ap.parse_args()

    root = os.path.abspath(args.repo_root)
    out = args.out or os.path.join(root, _GEN)
    os.makedirs(out, exist_ok=True)
    baseline_path = os.path.join(out, "known_discrepancies.txt")

    total_placed = 0
    findings = []
    for board, rel in BOARDS.items():
        path = os.path.join(root, rel)
        if not os.path.exists(path):
            emit(f"[skip] {board}: {rel} not present")
            continue

        rows, cpl = parse_board(path)
        total_placed += len(rows)
        write_csv(os.path.join(out, f"{board}_bom.csv"), rows, BOM_FIELDS)
        write_csv(os.path.join(out, f"{board}_cpl.csv"), cpl, CPL_FIELDS)

        summary = bom_summary(group_parts(rows))
        with open(os.path.join(out, f"{board}_bom_summary.csv"), "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=SUMMARY_FIELDS)
            w.writeheader()
            w.writerows(summary)

        emit(f"{board:12s} placed={len(rows):4d}  line-items={len(summary):3d}  "
             f"-> {os.path.relpath(os.path.join(out, board), root)}_bom.csv")

        # A placed part that has neither a value nor an MPN in the board file OR
        # in its BOM sheet cannot be ordered -- unless it is Do-Not-Populate, in
        # which case it is deliberately not ordered at all.  The DNP markers live
        # in the spreadsheets, so read the generated list (extract_dnp.py).
        dnp = set()
        dnp_list = os.path.join(out, "DNP_list.csv")
        if os.path.isfile(dnp_list):
            with open(dnp_list, newline="") as f:
                dnp = {r["Designator"] for r in csv.DictReader(f) if r.get("Designator")}

        sheet_data = {}
        for sheet_rel in SHEETS.get(board, []):
            data = read_sheet(os.path.join(root, sheet_rel))
            if data:
                sheet_data.update(data)

        unset = []
        for r in rows:
            if r["Designator"] in dnp:
                continue
            if any(m in ((r["Package"] or "") + (r["Library"] or "")).lower()
                   for m in MECHANICAL):
                continue
            s = sheet_data.get(r["Designator"], {})
            has = any((r["Value"], r["PartNo"], s.get("value", ""), s.get("mpn", "")))
            if not has:
                unset.append(r["Designator"])
        if unset:
            findings.append(f"{board}: {len(unset)} placed parts with no value/MPN "
                            f"anywhere (e.g. {', '.join(unset[:5])})")
        if dnp:
            emit(f"  ({len(dnp)} DNP designator(s) excluded from the orderability check)")

        for sheet_rel in SHEETS.get(board, []):
            sheet = read_sheet(os.path.join(root, sheet_rel))
            if sheet is None:
                continue
            name = os.path.basename(sheet_rel)
            board_refs = {r["Designator"] for r in rows}
            only_board = sorted(board_refs - set(sheet))
            only_sheet = sorted(set(sheet) - board_refs)
            emit(f"  vs {name}: {len(sheet)} designators in sheet, "
                 f"{len(only_board)} only on board, {len(only_sheet)} only in sheet")
            if only_board:
                findings.append(
                    f"{board}: {len(only_board)} designators on the board are absent "
                    f"from {name} (e.g. {', '.join(only_board[:8])})")
            if only_sheet:
                findings.append(
                    f"{board}: {len(only_sheet)} designators in {name} are absent "
                    f"from the board (e.g. {', '.join(only_sheet[:8])})")

    emit(f"\ntotal placed components across boards: {total_placed}")
    if findings:
        emit("\nFINDINGS")
        for f_ in findings:
            emit("  - " + f_)
    else:
        emit("\nNo BOM/board discrepancies found.")

    if args.update_baseline:
        with open(baseline_path, "w", newline="") as f:
            f.write("\n".join(sorted(findings)) + ("\n" if findings else ""))
        emit(f"\nbaseline updated: {os.path.relpath(baseline_path, root)} "
             f"({len(findings)} accepted findings)")
        return 0

    if not args.check:
        return 0

    # A gate that is red on arrival is useless: compare against the committed
    # baseline so CI fails on a NEW discrepancy (or on a stale baseline after a
    # fix), while the accepted ones stay visible in the output above.
    accepted = []
    if os.path.isfile(baseline_path):
        with open(baseline_path) as f:
            accepted = [ln.strip() for ln in f if ln.strip()]
    new = sorted(set(findings) - set(accepted))
    gone = sorted(set(accepted) - set(findings))
    emit(f"\nbaseline: {len(accepted)} accepted finding(s)")
    if new:
        emit(f"\nNEW discrepancies ({len(new)}):")
        for f_ in new:
            emit("  + " + f_)
    if gone:
        emit(f"\nRESOLVED since the baseline ({len(gone)}) -- update it with "
             f"--update-baseline:")
        for f_ in gone:
            emit("  - " + f_)
    if new or gone:
        return 1
    emit("no new discrepancies")
    return 0


if __name__ == "__main__":
    sys.exit(main())
