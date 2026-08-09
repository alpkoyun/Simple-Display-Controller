#!/usr/bin/env python3
"""Fail-closed repair for the four XDMA 4.1 BAR-hit decoder sites."""

from __future__ import annotations

import argparse
from pathlib import Path
import re


PATCHED_MARKER = "(m_axis_rx_tuser[8]) ? 3'h6"
OLD_DECODE = re.compile(
    r"\(m_axis_rx_tuser\[5\]\)\s*\?\s*3'h3\s*:\s*\n"
    r"(?P<indent>[ \t]*)\(m_axis_rx_tuser\[6\]\)\s*\?\s*3'h4\s*:\s*3'h5;"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", type=Path, required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--apply", action="store_true")
    return parser.parse_args()


def replacement(match: re.Match[str]) -> str:
    indent = match.group("indent")
    return (
        "(m_axis_rx_tuser[5]) ? 3'h3 :\n"
        f"{indent}(m_axis_rx_tuser[6]) ? 3'h4 :\n"
        f"{indent}(m_axis_rx_tuser[7]) ? 3'h5 :\n"
        f"{indent}(m_axis_rx_tuser[8]) ? 3'h6 : 3'h0;"
    )


def main() -> int:
    args = parse_args()
    target = args.target.resolve()
    text = target.read_text(encoding="utf-8")
    patched = text.count(PATCHED_MARKER)
    unpatched = len(list(OLD_DECODE.finditer(text)))

    if patched == 4 and unpatched == 0:
        print(f"TARGET={target}")
        print("PATCHED_DECODER_COUNT=4")
        print("SIMPLE_DISPLAY_XDMA_DECODER_PATCH_PASS")
        return 0
    if patched:
        raise SystemExit(
            f"FAIL: partial or mixed template: patched={patched}, unpatched={unpatched}"
        )
    if unpatched != 4:
        raise SystemExit(
            f"FAIL: unknown generated template: expected four sites, got {unpatched}"
        )
    if args.check:
        raise SystemExit("FAIL: four known decoder sites require the BAR6 patch")

    updated = OLD_DECODE.sub(replacement, text)
    if updated.count(PATCHED_MARKER) != 4:
        raise SystemExit("FAIL: internal patch verification failed")
    target.write_text(updated, encoding="utf-8")
    print(f"TARGET={target}")
    print("PATCHED_DECODER_COUNT=4")
    print("SIMPLE_DISPLAY_XDMA_DECODER_PATCH_PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
