#!/usr/bin/env python3
"""Summarize XDMA M_AXI_BYPASS transactions from a Vivado ILA CSV."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv_file", type=Path, help="Vivado ILA CSV capture")
    parser.add_argument(
        "--translation", type=lambda value: int(value, 0), default=0x3C000000
    )
    parser.add_argument(
        "--bar-offset", type=lambda value: int(value, 0), default=0x03FFF000
    )
    parser.add_argument(
        "--intended", type=lambda value: int(value, 0), default=0x3FFFF000
    )
    return parser.parse_args()


def find_column(fieldnames: list[str], suffix: str) -> str:
    matches = [name for name in fieldnames if suffix in name]
    if len(matches) != 1:
        raise ValueError(f"expected one column containing {suffix!r}, found {len(matches)}")
    return matches[0]


def asserted(row: dict[str, str], valid: str, ready: str) -> bool:
    return row[valid] == "1" and row[ready] == "1"


def main() -> int:
    args = parse_args()
    with args.csv_file.open(newline="", encoding="utf-8") as capture:
        reader = csv.DictReader(capture)
        if reader.fieldnames is None:
            raise ValueError("capture has no CSV header")
        fieldnames = reader.fieldnames
        rows = [row for row in reader if row["Sample in Buffer"].isdigit()]

    columns = {
        name: find_column(fieldnames, f"axi_{name}")
        for name in (
            "awaddr",
            "awvalid",
            "awready",
            "wdata",
            "wstrb",
            "wvalid",
            "wready",
            "bresp",
            "bvalid",
            "bready",
            "araddr",
            "arvalid",
            "arready",
            "rdata",
            "rresp",
            "rvalid",
            "rready",
        )
    }

    trigger_samples = [row["Sample in Buffer"] for row in rows if row["TRIGGER"] == "1"]
    aw_addresses: list[int] = []
    ar_addresses: list[int] = []
    b_responses: list[str] = []
    r_responses: list[str] = []

    print(f"capture={args.csv_file}")
    print(f"samples={len(rows)} trigger_samples={','.join(trigger_samples) or 'none'}")
    print("events:")
    for row in rows:
        events: list[str] = []
        if asserted(row, columns["awvalid"], columns["awready"]):
            address = int(row[columns["awaddr"]], 16)
            aw_addresses.append(address)
            events.append(f"AW=0x{address:016x}")
        if asserted(row, columns["wvalid"], columns["wready"]):
            events.append(
                f"W=0x{row[columns['wdata']]} strobe=0x{row[columns['wstrb']]}"
            )
        if asserted(row, columns["bvalid"], columns["bready"]):
            response = row[columns["bresp"]]
            b_responses.append(response)
            events.append(f"B={response}")
        if asserted(row, columns["arvalid"], columns["arready"]):
            address = int(row[columns["araddr"]], 16)
            ar_addresses.append(address)
            events.append(f"AR=0x{address:016x}")
        if asserted(row, columns["rvalid"], columns["rready"]):
            response = row[columns["rresp"]]
            r_responses.append(response)
            events.append(f"R=0x{row[columns['rdata']]} response={response}")
        if events:
            trigger = " trigger" if row["TRIGGER"] == "1" else ""
            print(f"  sample={int(row['Sample in Buffer']):4d}{trigger}: {' '.join(events)}")

    bitwise_address = args.translation | args.bar_offset
    additive_address = args.translation + args.bar_offset
    observed = sorted(set(aw_addresses + ar_addresses))
    print("translation:")
    print(f"  base                  0x{args.translation:08x}")
    print(f"  BAR offset            0x{args.bar_offset:08x}")
    print(f"  base OR offset        0x{bitwise_address:08x}")
    print(f"  base + offset         0x{additive_address:08x}")
    print(f"  intended address      0x{args.intended:08x}")
    print(
        "  observed addresses    "
        + (", ".join(f"0x{address:08x}" for address in observed) or "none")
    )
    print(f"  write responses       {','.join(b_responses) or 'none'}")
    print(f"  read responses        {','.join(r_responses) or 'none'}")

    if aw_addresses and aw_addresses[0] == bitwise_address and bitwise_address != args.intended:
        print("result=CONFIRMED_ALIAS: XDMA emitted base OR BAR offset, not the intended address")
        return 1
    if args.intended in observed and all(response == "OKAY" for response in b_responses + r_responses):
        print("result=PASS: intended AXI address completed with OKAY responses")
        return 0
    print("result=INCONCLUSIVE")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
