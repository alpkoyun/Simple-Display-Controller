#!/usr/bin/env python3
"""Summarize and compare VDMA-MM2S and MIG-slave AXI ILA captures."""

from __future__ import annotations

import argparse
import csv
from collections import Counter
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vdma_csv", type=Path)
    parser.add_argument("ddr_csv", type=Path)
    parser.add_argument(
        "--vdma-address", type=lambda value: int(value, 0), default=0x3E000000
    )
    parser.add_argument(
        "--ddr-address", type=lambda value: int(value, 0), default=0x00000000
    )
    return parser.parse_args()


def find_column(fieldnames: list[str], suffix: str, *, required: bool = True) -> str | None:
    matches = [name for name in fieldnames if suffix in name]
    if len(matches) == 1:
        return matches[0]
    if not required and not matches:
        return None
    raise ValueError(f"expected one column containing {suffix!r}, found {len(matches)}")


def load_capture(path: Path) -> tuple[list[str], list[dict[str, str]]]:
    with path.open(newline="", encoding="utf-8") as capture:
        reader = csv.DictReader(capture)
        if reader.fieldnames is None:
            raise ValueError(f"{path}: capture has no CSV header")
        fields = reader.fieldnames
        rows = [row for row in reader if row["Sample in Buffer"].isdigit()]
    return fields, rows


def is_high(value: str) -> bool:
    return value == "1"


def summarize(label: str, path: Path) -> dict[str, object]:
    fields, rows = load_capture(path)
    names = {
        name: find_column(fields, f"axi_{name}", required=name not in {"rlast"})
        for name in (
            "araddr", "arlen", "arsize", "arvalid", "arready",
            "rdata", "rresp", "rvalid", "rready", "rlast",
        )
    }

    ar_events: list[tuple[int, int, int, int]] = []
    r_events: list[tuple[int, str, str, bool]] = []
    for row in rows:
        sample = int(row["Sample in Buffer"])
        if is_high(row[names["arvalid"]]) and is_high(row[names["arready"]]):
            ar_events.append(
                (
                    sample,
                    int(row[names["araddr"]], 16),
                    int(row[names["arlen"]], 16),
                    int(row[names["arsize"]].split()[0], 0)
                    if " " not in row[names["arsize"]]
                    else int(row[names["arsize"]].split()[0]),
                )
            )
        if is_high(row[names["rvalid"]]) and is_high(row[names["rready"]]):
            rlast = bool(names["rlast"] and is_high(row[names["rlast"]]))
            r_events.append(
                (sample, row[names["rresp"]], row[names["rdata"]], rlast)
            )

    # Vivado formats ARSIZE either as a numeric code or as text such as
    # "32 bytes". Re-read it defensively for the human-facing event list.
    def size_text(row: dict[str, str]) -> str:
        return row[names["arsize"]]

    print(f"{label}: capture={path}")
    trigger = [row["Sample in Buffer"] for row in rows if row["TRIGGER"] == "1"]
    print(f"  samples={len(rows)} trigger={','.join(trigger) or 'none'}")
    print(f"  accepted_AR={len(ar_events)} accepted_R={len(r_events)}")
    print(
        "  RRESP="
        + (", ".join(f"{key}:{count}" for key, count in Counter(e[1] for e in r_events).items()) or "none")
    )
    print(f"  RLAST={sum(event[3] for event in r_events)}")

    shown = 0
    print("  read-address events:")
    for row in rows:
        if not (is_high(row[names["arvalid"]]) and is_high(row[names["arready"]])):
            continue
        address = int(row[names["araddr"]], 16)
        arlen = int(row[names["arlen"]], 16)
        mark = " trigger" if row["TRIGGER"] == "1" else ""
        print(
            f"    sample={int(row['Sample in Buffer']):4d}{mark} "
            f"AR=0x{address:08x} ARLEN={arlen} ARSIZE={size_text(row)}"
        )
        shown += 1
        if shown == 12:
            remaining = len(ar_events) - shown
            if remaining > 0:
                print(f"    ... {remaining} more accepted AR requests")
            break

    first_data = [event for event in r_events[:4]]
    if first_data:
        print("  first read-data beats:")
        for sample, response, data, rlast in first_data:
            print(
                f"    sample={sample:4d} RRESP={response} RLAST={int(rlast)} "
                f"RDATA=0x{data}"
            )

    return {"ar": ar_events, "r": r_events}


def main() -> int:
    args = parse_args()
    vdma = summarize("VDMA_MM2S", args.vdma_csv)
    ddr = summarize("DDR3_SLAVE", args.ddr_csv)

    vdma_ar = vdma["ar"]
    ddr_ar = ddr["ar"]
    vdma_ok = bool(vdma_ar) and bool(vdma["r"]) and all(e[1] == "OKAY" for e in vdma["r"])
    ddr_ok = bool(ddr_ar) and bool(ddr["r"]) and all(e[1] == "OKAY" for e in ddr["r"])

    print("comparison:")
    vdma_addresses = [event[1] for event in vdma_ar]
    ddr_addresses = [event[1] for event in ddr_ar]
    print(f"  expected VDMA address    0x{args.vdma_address:08x}")
    print(f"  expected DDR address     0x{args.ddr_address:08x}")
    if vdma_addresses:
        print(f"  observed VDMA range      0x{min(vdma_addresses):08x}-0x{max(vdma_addresses):08x}")
    if ddr_addresses:
        print(f"  observed DDR range       0x{min(ddr_addresses):08x}-0x{max(ddr_addresses):08x}")

    if not vdma_ar:
        print("result=FAIL_VDMA_NO_READ_REQUESTS")
        return 1
    if not ddr_ar:
        print("result=FAIL_ROUTING_NO_DDR_REQUESTS")
        return 1
    if not ddr_ok:
        print("result=FAIL_DDR_READ_RESPONSE")
        return 1
    if not vdma_ok:
        print("result=FAIL_VDMA_READ_RESPONSE")
        return 1
    if args.vdma_address not in vdma_addresses or args.ddr_address not in ddr_addresses:
        print("result=FAIL_UNEXPECTED_ADDRESS_TRANSLATION")
        return 1

    print("result=PASS_VDMA_TO_DDR_READ_PATH")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
