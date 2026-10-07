#!/usr/bin/env python3
"""Every part number in the production BOMs, with what is actually known about it.

Issue #22 asks that every production part number exists and is orderable.  Two things can be
established from the repo alone, and they are worth establishing mechanically rather than by
eye:

  * **format** - a part number with whitespace in it, a placeholder token, or a value that
    disagrees with the part's own family is not orderable as written, and the row-identity check
    in regenerate_bom.py already showed how easily a wrong-but-plausible string survives;
  * **coverage** - which numbers have been checked against a distributor and which have not.

What it cannot do is reach a distributor, so it does not pretend to: it emits the inventory and
the format verdicts, and it reads a checked-in list of numbers that have been verified live
(bom_verified_mpns.txt) so the remaining gap is a number rather than an impression.

Usage:
    bom_orderability.py [<repo-root>] [--out <dir>] [--check]
Writes <out>/bom_orderability.csv.  --check exits non-zero when the file is stale.
"""
import csv
import io
import os
import pathlib
import re
import sys

_PF = "4_Schematics and Boards Layout/4_7_Production Files"
BOMS = [
    ("Main board", f"{_PF}/Gerber_Main_Board/BOM_Main_Board.xlsx"),
    ("Power board", f"{_PF}/Gerber_PowerBoard/BOM_Power_Board.xlsx"),
    ("PA board", f"{_PF}/Gerber_PA/BOM_PA.xlsx"),
    ("Frequency synthesizer", f"{_PF}/Gerber_Freq_Synth/BOM_Freq_Synth.xlsx"),
    ("Patch antenna", f"{_PF}/Gerber_Patch_Antenna/BOM_Patch_Antenna.xlsx"),
]
OUT = "4_Schematics and Boards Layout/4_7_Production Files/Board_Artifacts/bom_orderability.csv"
VERIFIED = "9_Firmware/tools/bom_verified_mpns.txt"
FIELDS = ["Board", "Manufacturer", "MPN", "Qty", "Parts", "FormatStatus", "Verified", "Note"]

PLACEHOLDER = re.compile(r"^(tbd|tba|n/?a|dnp|none|\?+|-+)$", re.I)
# Part numbers are alphanumeric plus a few separators.  '#' and '=' are included deliberately:
# they are real, not noise - Analog Devices uses '#' for packaging (LTC5552IUDB#TRMPBF) and
# TDK/Chilisin use '=' for a suffix (DEM8045Z-2R2N=P3).  Whitespace is the thing that never
# belongs in a part number.
LEGAL = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+/#=-]*$")


def say(msg):
    """Write a line to stdout (the repo lints print() away with flake8-print)."""
    sys.stdout.write(msg + "\n")


def read_bom(path):
    import openpyxl
    ws = openpyxl.load_workbook(path, data_only=True).active
    rows = list(ws.iter_rows(values_only=True))
    if not rows:
        return []
    header = ["" if c is None else str(c).strip() for c in rows[0]]
    idx = {h: i for i, h in enumerate(header)}
    out = []
    for row in rows[1:]:
        cells = ["" if c is None else str(c).strip() for c in row]
        mpn = cells[idx.get("MANUFACTURER_PART_NUMBER", 0)] if idx else ""
        if not mpn:
            continue
        out.append({
            "Manufacturer": cells[idx.get("MANUFACTURER", 0)] if "MANUFACTURER" in idx else "",
            "MPN": mpn,
            "Qty": cells[idx.get("Qty", 0)] if "Qty" in idx else "",
            "Parts": cells[idx.get("Parts", 0)] if "Parts" in idx else "",
        })
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    check = "--check" in sys.argv
    root = pathlib.Path(args[0] if args else ".")
    out_dir = root / "4_Schematics and Boards Layout/4_7_Production Files/Board_Artifacts"
    out_path = root / OUT

    verified = set()
    vpath = root / VERIFIED
    if vpath.exists():
        verified = {ln.strip() for ln in vpath.read_text().splitlines()
                    if ln.strip() and not ln.startswith("#")}

    # Do-Not-Populate parts are deliberately not ordered, so they do not belong in an
    # orderability check; extract_dnp.py already identifies them.
    dnp = set()
    dpath = out_dir / "DNP_list.csv"
    if dpath.exists():
        import csv as _csv
        with open(dpath, newline="") as f:
            dnp = {r["Designator"] for r in _csv.DictReader(f) if r.get("Designator")}

    rows = []
    for board, rel in BOMS:
        path = root / rel
        if not path.exists():
            continue
        for r in read_bom(path):
            refs = [x.strip() for x in (r["Parts"] or "").split(",") if x.strip()]
            if refs and all(x in dnp for x in refs):
                continue
            mpn = r["MPN"]
            note = ""
            if PLACEHOLDER.match(mpn):
                status, note = "placeholder", "a placeholder is not a part number"
            elif re.search(r"\s", mpn):
                status = "needs-check"
                note = ("whitespace in the part number - legitimate for some manufacturers "
                        "(ams OSRAM KB EELP41.12-...), but it has to be confirmed rather than "
                        "assumed")
            elif not LEGAL.match(mpn):
                status, note = "illegal-characters", "punctuation a distributor will not match"
            elif len(mpn) < 4:
                status, note = "too-short", "shorter than any real part number"
            else:
                status = "format-ok"
            rows.append({
                "Board": board, "Manufacturer": r["Manufacturer"], "MPN": mpn,
                "Qty": r["Qty"], "Parts": r["Parts"], "FormatStatus": status,
                "Verified": "yes" if mpn in verified else "no", "Note": note,
            })

    rows.sort(key=lambda r: (r["Board"], r["MPN"]))
    buf = io.StringIO()
    w = csv.DictWriter(buf, fieldnames=FIELDS, lineterminator="\n")
    w.writeheader()
    w.writerows(rows)
    text = buf.getvalue()

    bad = [r for r in rows if r["FormatStatus"] != "format-ok"]
    unverified = [r for r in rows if r["Verified"] == "no"]
    distinct = {r["MPN"] for r in rows}
    say(f"{len(rows)} part-number rows across {len({r['Board'] for r in rows})} boards, "
        f"{len(distinct)} distinct part numbers")
    say(f"  format problems: {len(bad)}")
    for r in bad[:8]:
        say(f"    {r['Board']}: {r['MPN']} ({r['FormatStatus']}) - {r['Note']}")
    say(f"  checked against a distributor: {len(rows) - len(unverified)}")
    say(f"  not yet checked:              {len(unverified)}")

    if check:
        current = out_path.read_text() if out_path.exists() else None
        if current != text:
            say("stale: regenerate bom_orderability.csv")
            return 1
        say("bom_orderability.csv is current")
        # A "needs-check" entry is reported, not failed on: it is a judgement call for a human
        # (whitespace in an ams OSRAM number is legitimate).  Only a placeholder or an
        # unorderable string is a hard error.
        hard_statuses = ("placeholder", "illegal-characters", "too-short")
        hard = [r for r in bad if r["FormatStatus"] in hard_statuses]
        return 1 if hard else 0

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w", newline="") as f:
        f.write(text)
    say(f"wrote {os.path.relpath(out_path, root)}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
