#!/usr/bin/env python3
"""Netlist integrity gate for the Eagle boards.

The BOM/DNP/rail gates all passed while the power board carried two `<element>`s named
X36 -- one of them with no schematic counterpart at all -- and while two different nets
claimed the same connector pins.  None of those tools looks at element identity or pin
ownership, so this one does.

Checks, per board (.sch + .brd pair):
  1. duplicate reference designators among schematic parts
  2. duplicate reference designators among board elements
  3. a schematic pin claimed by more than one net
  4. a board element with no schematic part   (an orphan: nothing generates its netlist)
  5. a schematic part with no board element   (unplaced: cannot be built)
  6. a pinref naming a gate the part does not instantiate
  7. a board element wired to nothing (no contactref in any signal)

Supply and frame symbols (library supply1/supply2/frames) are exempt from 4 and 5: they are
schematic-only by design.  A board that ships gerbers without a layout is exempt from 5
(and skipped entirely when neither file is present).

Usage:
    netlist_integrity.py [<repo-root>] [--check]
Exit code is 0 when every board is clean, 1 otherwise.  --check prints the same report and
never writes anything (this tool has no write mode at all).
"""
import re
import xml.etree.ElementTree as ET
import sys
import pathlib

BOARDS = [
    ("Main board", "4_Schematics and Boards Layout/4_6_Schematics/MainBoard/RADAR_Main_Board"),
    ("Power board", "4_Schematics and Boards Layout/4_6_Schematics/PowerBoard/PowerBoard"),
    ("PA board", "4_Schematics and Boards Layout/4_6_Schematics/PowerAmplifierBoard/RF_PA"),
]

EXEMPT_LIB = re.compile(r"^(supply\d*|frames?|frames_.*)$")


def say(msg):
    """Write a line to stdout (the repo lints print() away with flake8-print)."""
    sys.stdout.write(msg + "\n")


def collect(sch_text, brd_text):
    """Everything the checks need, extracted once."""
    parts, exempt = [], set()
    for m in re.finditer(r'<part name="([^"]+)"\s+library="([^"]+)"', sch_text):
        if EXEMPT_LIB.match(m.group(2)):
            exempt.add(m.group(1))
        else:
            parts.append(m.group(1))
    parts = [p for p in parts if p not in exempt]

    elements = re.findall(r'<element name="([^"]+)"', brd_text)

    instances = {}
    for m in re.finditer(r'<instance part="([^"]+)"\s+gate="([^"]+)"', sch_text):
        instances.setdefault(m.group(1), set()).add(m.group(2))

    netpins = {}
    for m in re.finditer(r'<net name="([^"]+)"[^>]*>(.*?)</net>', sch_text, re.S):
        netpins[m.group(1)] = re.findall(
            r'<pinref part="([^"]+)" gate="([^"]+)" pin="([^"]+)"/>', m.group(2))

    sigrefs = {}
    for m in re.finditer(r'<signal name="([^"]+)"[^>]*>(.*?)</signal>', brd_text, re.S):
        sigrefs[m.group(1)] = re.findall(
            r'<contactref element="([^"]+)" pad="([^"]+)"/>', m.group(2))

    return parts, elements, instances, netpins, sigrefs, exempt


def duplicates(names):
    seen, dupes = set(), []
    for n in names:
        if n in seen and n not in dupes:
            dupes.append(n)
        seen.add(n)
    return dupes


def check_board(_label, sch_path, brd_path):
    if not sch_path.exists() or not brd_path.exists():
        missing = sch_path.name if not sch_path.exists() else brd_path.name
        return [f"missing file: {missing}"]
    # 0 the files must be well-formed XML.  Every other check below is regex-based, so a
    #   mis-spliced tag - an edit that dropped a closing tag, or one that landed inside an
    #   attribute value - passes all of them while breaking every XML consumer of the file.
    for path in (sch_path, brd_path):
        try:
            ET.fromstring(path.read_text(errors="replace"))
        except ET.ParseError as exc:
            return [f"{path.name} is not well-formed XML: {exc}"]

    parts, elements, instances, netpins, sigrefs, exempt = collect(
        sch_path.read_text(errors="replace"), brd_path.read_text(errors="replace"))
    problems = []

    # 1/2 duplicate designators
    for tag, names in (("schematic part", parts), ("board element", elements)):
        problems.extend(f"duplicate {tag} designator: {d} (x{names.count(d)})"
                        for d in duplicates(names))

    # 3 a schematic pin claimed by two nets
    owners = {}
    for net, pins in netpins.items():
        for part, gate, pin in pins:
            owners.setdefault((part, gate, pin), []).append(net)
    problems.extend(
        f"pin {part}.{pin} (gate {gate}) claimed by {len(set(nets))} nets: "
        f"{', '.join(sorted(set(nets)))}"
        for (part, gate, pin), nets in sorted(owners.items()) if len(set(nets)) > 1)

    # 4/5 orphan and unplaced parts
    pset, eset = set(parts) - exempt, set(elements)
    problems.extend(f"board element {e} has no schematic part (orphan: no netlist source)"
                    for e in sorted(eset - pset))
    problems.extend(f"schematic part {p} has no board element (unplaced)"
                    for p in sorted(pset - eset))

    # 6 a pinref naming a gate the part never instantiates
    problems.extend(
        f"pinref {part}.{pin} in net {net} names gate '{gate}', "
        f"but the part instantiates {sorted(instances[part])}"
        for net, pins in netpins.items() for part, gate, pin in pins
        if instances.get(part) and gate not in instances[part])

    # 7 a board element wired to nothing
    wired = {e for refs in sigrefs.values() for e, _pad in refs}
    problems.extend(f"board element {e} has no contactref in any signal (unconnected)"
                    for e in sorted(eset - wired))

    # 8 a schematic net with no board signal of the same name.  Renaming a net on one side
    # only (a rail rename that misses the board file) leaves the two files disagreeing while
    # every other check stays green.
    sig_names = set(sigrefs)
    problems.extend(
        f"schematic net {net} has no board signal of that name "
        f"({len(pins)} pin(s), e.g. {pins[0][0]}.{pins[0][2]})"
        for net, pins in sorted(netpins.items())
        if net not in sig_names and any(p[0] in eset for p in pins))
    return problems


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    root = pathlib.Path(args[0] if args else ".")
    checked = failed = 0
    for label, stem in BOARDS:
        sch, brd = root / (stem + ".sch"), root / (stem + ".brd")
        if not sch.exists() and not brd.exists():
            continue
        checked += 1
        problems = check_board(label, sch, brd)
        if problems:
            failed += 1
            say(f"[FAIL] {label}")
            for p in problems:
                say(f"   - {p}")
        else:
            say(f"[ok]   {label}")
    say(f"\n{checked - failed} board(s) clean, {failed} with problems (of {checked} checked)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
