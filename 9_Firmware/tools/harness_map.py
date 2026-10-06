#!/usr/bin/env python3
"""Harness map: which rail leaves which board on which connector, and on which pin.

Why: 25 rails cross board boundaries and every interconnect is the same 2-position Molex KK
part, so the only thing that says what a plug carries is the schematic. A harness built from
memory or from a stale spreadsheet is how a 22 V drain feed ends up in a 3.3 V socket. This
tool emits the wiring list from the boards themselves, and --check fails when the emitted
file is stale.

The keying column is now generated from the connector part itself: ordinary positive rails keep
the 2-position housing, the negative rails use the 3-position part and the PA drain feeds the
4-position part, so a plug can no longer reach a socket of the wrong class. The column is read
from the netlist rather than typed, so it cannot drift from the boards.

Usage:
    harness_map.py [<repo-root>] [--check]
Writes 4_Schematics and Boards Layout/4_7_Production Files/Board_Artifacts/harness_map.csv
Exit code 0 on success (or when --check finds the file current), 1 when stale or on error.
"""
import csv
import io
import re
import sys
import pathlib

BOARDS = [
    ("Main board", "MainBoard/RADAR_Main_Board"),
    ("Power board", "PowerBoard/PowerBoard"),
    ("Frequency synthesizer", "FrequencySynthesizerBoard/Clocks_Freq_Synth_board"),
    ("Power amplifier", "PowerAmplifierBoard/RF_PA"),
]
SCH_ROOT = "4_Schematics and Boards Layout/4_6_Schematics"
OUT = ("4_Schematics and Boards Layout/4_7_Production Files/Board_Artifacts/harness_map.csv")
RAILS = ("4_Schematics and Boards Layout/4_7_Production Files/Board_Artifacts/interboard_rails.csv")
CONNECTOR_DEVICESETS = ("22-23-2021", "22-23-2031", "22-23-2041", "AK300/2")
# keying class, derived from the housing the rail is wired to (see the module docstring)
KEYING = {"22-23-2021": "2-pos", "22-23-2031": "3-pos", "22-23-2041": "4-pos"}


def say(msg):
    """Write a line to stdout (the repo lints print() away with flake8-print)."""
    sys.stdout.write(msg + "\n")


def rail_boards(root):
    """rail -> boards it appears on, from the generated rail artifact (if present)."""
    p = root / RAILS
    if not p.exists():
        return {}
    out = {}
    with p.open() as fh:
        for row in csv.DictReader(fh):
            out[row["Rail"]] = row["Boards"].split()
    return out


def connector_pins(text, ref):
    """pin -> net for one connector part, from the schematic's nets."""
    pins = {}
    for m in re.finditer(r'<net name="([^"]+)"[^>]*>(.*?)</net>', text, re.S):
        pattern = (r'<pinref part="' + re.escape(ref)
                   + r'" gate="([^"]+)" pin="([^"]+)"/>')
        for gate, pin in re.findall(pattern, m.group(2)):
            pins[f"{gate}.{pin}"] = m.group(1)
    return pins


def build(root):
    boards_of = rail_boards(root)
    rows = []
    for label, stem in BOARDS:
        sch = root / SCH_ROOT / (stem + ".sch")
        if not sch.exists():
            continue
        text = sch.read_text(errors="replace")
        conns = re.findall(r'<part name="([^"]+)"[^>]*deviceset="([^"]+)"', text)
        for ref, ds in sorted(conns, key=lambda t: (len(t[0]), t[0])):
            if ds not in CONNECTOR_DEVICESETS:
                continue
            for pin, net in sorted(connector_pins(text, ref).items()):
                others = [b for b in boards_of.get(net, []) if b != label.replace(" ", "")]
                rows.append({
                    "Board": label,
                    "Connector": ref,
                    "Part": ds,
                    "Pin": pin,
                    "Net": net,
                    "CounterpartBoard": " ".join(others) if others else "local",
                    "Keying": KEYING.get(ds, "unkeyed"),
                    "Notes": (f"rail spans {len(boards_of[net])} boards"
                              if net in boards_of and len(boards_of[net]) > 1 else ""),
                })
    return rows


def render(rows):
    buf = io.StringIO()
    w = csv.DictWriter(buf, fieldnames=list(rows[0].keys()), lineterminator="\n")
    w.writeheader()
    w.writerows(rows)
    return buf.getvalue()


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    check = "--check" in sys.argv
    root = pathlib.Path(args[0] if args else ".")
    rows = build(root)
    if not rows:
        say("harness_map: no connectors found (wrong root?)")
        return 1
    text = render(rows)
    out = root / OUT
    if check:
        current = out.read_text() if out.exists() else ""
        if current != text:
            say(f"harness_map: {out} is stale ({len(rows)} rows to write)")
            return 1
        say(f"harness_map: current ({len(rows)} rows)")
        return 0
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text)
    connectors = len({(r["Board"], r["Connector"]) for r in rows})
    say(f"harness_map: wrote {len(rows)} pin assignments across {connectors} connectors")
    return 0


if __name__ == "__main__":
    sys.exit(main())
