#!/usr/bin/env python3
"""Check or repair the AX7203 PCIe GT lane LOC order in generated XDC files."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


LANE_RE = re.compile(
    r"(set_property\s+LOC\s+GTPE2_CHANNEL_X0Y)(\d+)(\s+\[get_cells\s+\{.*?pipe_lane\[)(\d+)(\].*?\}\]\s*)$"
)


def candidate_files(root: Path) -> list[Path]:
    patterns = ("*s0y0.xdc", "*S0Y0.xdc", "*PCIE_X0Y0.xdc", "*pcie*x0y0*.xdc")
    found: set[Path] = set()
    for pattern in patterns:
        found.update(root.rglob(pattern))

    for path in root.rglob("*.xdc"):
        if path in found:
            continue
        try:
            text = path.read_text(errors="ignore")
        except OSError:
            continue
        if "GTPE2_CHANNEL" in text and "pipe_lane[" in text:
            found.add(path)

    return sorted(found)


def parse_order(lines: list[str]) -> dict[int, int]:
    order: dict[int, int] = {}
    for line in lines:
        match = LANE_RE.match(line)
        if match:
            gt_y = int(match.group(2))
            lane = int(match.group(4))
            order[lane] = gt_y
    return order


def fix_lines(lines: list[str], expected: dict[int, int]) -> tuple[list[str], bool]:
    changed = False
    fixed: list[str] = []
    for line in lines:
        match = LANE_RE.match(line)
        if not match:
            fixed.append(line)
            continue

        lane = int(match.group(4))
        if lane not in expected:
            fixed.append(line)
            continue

        wanted = str(expected[lane])
        if match.group(2) != wanted:
            line = "".join((match.group(1), wanted, match.group(3), match.group(4), match.group(5)))
            changed = True
        fixed.append(line)

    return fixed, changed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path, help="Project or generated-IP root to scan")
    parser.add_argument("--expected", default="5,4,6,7", help="Expected GT Y order for pipe_lane[0..3]")
    parser.add_argument("--fix", action="store_true", help="Rewrite generated XDC LOC lines if needed")
    args = parser.parse_args()

    expected_values = [int(item) for item in args.expected.split(",")]
    if len(expected_values) != 4:
        raise SystemExit("--expected must contain four comma-separated integers")
    expected = {idx: value for idx, value in enumerate(expected_values)}

    root = args.root.resolve()
    files = candidate_files(root)
    if not files:
        print(f"ERROR: no PCIe GT lane XDC candidate found under {root}", file=sys.stderr)
        return 1

    ok = True
    touched = False
    for path in files:
        lines = path.read_text(errors="ignore").splitlines(keepends=True)
        order = parse_order(lines)
        if not order:
            continue

        observed = [order.get(idx) for idx in range(4)]
        if observed == expected_values:
            print(f"OK: {path}: pipe_lane[0..3] -> {observed}")
            continue

        if args.fix and all(idx in order for idx in range(4)):
            fixed, changed = fix_lines(lines, expected)
            if changed:
                path.write_text("".join(fixed))
                touched = True
            lines = path.read_text(errors="ignore").splitlines(keepends=True)
            order = parse_order(lines)
            observed = [order.get(idx) for idx in range(4)]
            if observed == expected_values:
                print(f"FIXED: {path}: pipe_lane[0..3] -> {observed}")
                continue

        print(
            f"ERROR: {path}: pipe_lane[0..3] -> {observed}, expected {expected_values}",
            file=sys.stderr,
        )
        ok = False

    if touched:
        print("INFO: regenerated PCIe GT lane XDC was updated; keep this check before synthesis.")

    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
