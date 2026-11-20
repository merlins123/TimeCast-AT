#!/usr/bin/env python3
"""Build fixed-length or AT experiment firmware, one image per logical node."""

import argparse
from collections import Counter
from fractions import Fraction
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys


APP_DIR = Path(__file__).resolve().parent


def allocate_lengths(count, maximum, classes, percent):
    """Keep node 0 at maximum; minimize the other nodes' capacity range."""
    target = count * maximum * percent / 100
    if target.denominator != 1:
        raise ValueError(f"Target total is {target} bytes; integer payloads cannot match it exactly")
    target = int(target)
    capacities = sorted({(c + 1) * maximum // classes for c in range(classes)})
    remaining = count - 1
    total = target - maximum
    if remaining == 0:
        if total != 0:
            raise ValueError("A single node must send the maximum: AVG_PERCENT must be 100")
        return [maximum]
    minimum_total = maximum + remaining * capacities[0]
    if not minimum_total <= target <= count * maximum:
        minimum_percent = 100 * minimum_total / (count * maximum)
        raise ValueError(
            f"With one maximum-length node, the minimum average is "
            f"{minimum_total / count:g} B ({minimum_percent:g}%); "
            f"class capacities are {capacities}"
        )

    # Try narrow capacity ranges first so the shorter nodes stay close in size.
    intervals = [capacities[i:j + 1]
                 for i in range(len(capacities)) for j in range(i, len(capacities))
                 if remaining * capacities[i] <= total <= remaining * capacities[j]]
    intervals.sort(key=lambda values: (
        values[-1] - values[0],
        abs(remaining * (values[0] + values[-1]) - 2 * total), values[0]))
    for values in intervals:
        low = values[0]
        wanted = total - remaining * low
        mask = (1 << (wanted + 1)) - 1
        reachable = [1]
        for _ in range(remaining):
            sums = 0
            for value in values:
                sums |= reachable[-1] << (value - low)
            reachable.append(sums & mask)
        if not (reachable[-1] >> wanted) & 1:
            continue

        lengths = [maximum]
        for left in range(remaining, 0, -1):
            choices = sorted(values, key=lambda value: (
                abs(left * (value - low) - wanted), value))
            for value in choices:
                rest = wanted - (value - low)
                if rest >= 0 and (reachable[left - 1] >> rest) & 1:
                    lengths.append(value)
                    wanted = rest
                    break
        return lengths
    raise ValueError(
        f"Target total {target} B is unreachable using class capacities {capacities} "
        "and at least one maximum-length node; change AVG_PERCENT or CLASS_COUNT"
    )


def read_nodes(path):
    """Read the existing mixed-board mapping: logical_id board physical_id."""
    nodes = []
    seen = set()
    for line_number, line in enumerate(path.read_text().splitlines(), 1):
        fields = line.split("#", 1)[0].split()
        if not fields:
            continue
        if len(fields) != 3 or fields[0] != str(len(nodes)):
            raise ValueError(f"{path}:{line_number}: expected logical ID {len(nodes)}, board, physical ID")
        _, board, physical = fields
        if board not in ("nrf52840dk", "nrf52dk"):
            raise ValueError(f"Unsupported mixed board: {board}")
        if (not physical.isascii() or not physical.isdigit() or
                physical.startswith("0")):
            raise ValueError(f"Invalid physical node ID: {physical}")
        key = (board, int(physical))
        if key in seen:
            raise ValueError(f"Duplicate physical node: {board}-{physical}")
        seen.add(key)
        nodes.append({"id": len(nodes), "board": board, "physical_id": str(int(physical))})
    return nodes


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__, epilog=(
        "AT uses class-capacity endpoints, and logical node 0 always sends the maximum. "
        "P1 NTX and other timing parameters remain controlled by the Makefile/environment. "
        "--nodes-file accepts nodes_saclay_mixed.txt; its rows determine the node count. "
        "Dry-run prints the allocation and make commands without writing files."))
    parser.add_argument("--mode", choices=("at", "original"), required=True)
    parser.add_argument("--max-data-len", type=int, required=True, metavar="BYTES")
    parser.add_argument("--class-count", type=int, required=True, metavar="COUNT")
    parser.add_argument("--avg-percent", default=os.environ.get("AVG_PERCENT", "100"),
                        help="AT network mean as a percentage of maximum; default: AVG_PERCENT or 100")
    parser.add_argument("--count", type=int, help="uniform-board node count (default: 40)")
    parser.add_argument("--board", help="uniform board (default: dwm1001)")
    parser.add_argument("--nodes-file", type=Path, help="mixed-board mapping file")
    parser.add_argument("--ntx", type=int, default=10, help="P2 NTX only (default: 10)")
    parser.add_argument("--jobs", type=int, default=os.environ.get("JOBS", "4"))
    parser.add_argument("--out", type=Path, default=os.environ.get("OUT"))
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.max_data_len <= 110:
        parser.error("--max-data-len must be 1..110 (the current store buffer limit)")
    if not 1 <= args.class_count <= 16:
        parser.error("--class-count must be 1..16")
    if not 1 <= args.ntx <= 127 or args.jobs < 1:
        parser.error("--ntx must be 1..127 and --jobs must be positive")
    try:
        args.percent = Fraction(args.avg_percent)
    except (ValueError, ZeroDivisionError):
        parser.error("--avg-percent must be a number in (0, 100]")
    if not 0 < args.percent <= 100:
        parser.error("--avg-percent must be in (0, 100]")
    if args.mode == "original" and args.percent != 100:
        parser.error("original mode sends MAX_DATA_LEN on every node; --avg-percent must be 100")
    if args.nodes_file and (args.count is not None or args.board is not None):
        parser.error("--nodes-file supplies both the node count and boards; omit --count and --board")
    return args


def main():
    args = parse_args()
    if args.nodes_file:
        nodes = read_nodes(args.nodes_file)
        board_tag = "mixed"
    else:
        count = args.count if args.count is not None else 40
        board_tag = args.board or "dwm1001"
        if board_tag not in ("dwm1001", "nrf52840dk", "nrf52dk"):
            raise ValueError("--board must be dwm1001, nrf52840dk or nrf52dk")
        if not 1 <= count <= 255:
            raise ValueError("Node count must be 1..255")
        nodes = [{"id": i, "board": board_tag, "physical_id": None} for i in range(count)]
    count = len(nodes)
    if not 1 <= count <= 255:
        raise ValueError("Node count must be 1..255")

    lengths = ([args.max_data_len] * count if args.mode == "original" else
               allocate_lengths(count, args.max_data_len, args.class_count, args.percent))
    percent_tag = str(args.percent).replace("/", "of")
    out = (args.out or APP_DIR / "firmwares" /
           f"{board_tag}-{args.mode}-n{count}-x{args.ntx}-max{args.max_data_len}"
           f"-c{args.class_count}-avg{percent_tag}").resolve()
    commands = []
    for node, length in zip(nodes, lengths):
        class_id = ((length * args.class_count + args.max_data_len - 1) //
                    args.max_data_len - 1) if length else 0
        node.update(data_len=length, class_id=class_id,
                    capacity=(class_id + 1) * args.max_data_len // args.class_count)
        node["firmware"] = str(out / f"id-{node['id']}" / node["board"] / "timecast_at.elf")
        commands.append([
            "make", "-C", str(APP_DIR), f"-j{args.jobs}", "all",
            f"BOARD={node['board']}", f"LOCAL_NODE_ID={node['id']}",
            f"TIMECAST_P2_NODE_COUNT={count}", f"TIMECAST_STORE_MAX_NODES={count}",
            f"TIMECAST_P2_NTX={args.ntx}", f"TIMECAST_USE_AT={int(args.mode == 'at')}",
            f"TIMECAST_MAX_DATA_LEN={args.max_data_len}",
            f"TIMECAST_CLASS_COUNT={args.class_count}", f"TIMECAST_APP_DATA_LEN={length}",
            "TIMECAST_TEST_NODE2_CLASS_PERIOD=0", f"BINDIRBASE={out / ('id-' + str(node['id']))}",
        ])

    print(f"Mode: {args.mode}; nodes: {count}; P2 NTX: {args.ntx}")
    print(f"Payload: max={max(lengths)} B, mean={sum(lengths) / count:g} B ({args.percent}%)")
    print("Distribution: " + ", ".join(f"{n} x {length} B" for length, n in sorted(Counter(lengths).items())))
    print(f"Firmware directory: {out}")
    print("logical_id\tboard\tphysical_id\tdata_len\tclass_id\tcapacity")
    for node in nodes:
        print("\t".join(str(node[key]) if node[key] is not None else "-"
                        for key in ("id", "board", "physical_id", "data_len", "class_id", "capacity")))
    if args.dry_run:
        for command in commands:
            print(shlex.join(command))
        return

    out.mkdir(parents=True, exist_ok=True)
    # Only a fully successful batch gets a build manifest, including on rebuild.
    for name in ("experiment-build.json", "mixed-build.tsv"):
        (out / name).unlink(missing_ok=True)
    for node, command in zip(nodes, commands):
        print(shlex.join(command), flush=True)
        subprocess.run(command, check=True)
        firmware = Path(node["firmware"])
        if not firmware.is_file() or firmware.stat().st_size == 0:
            raise ValueError(f"Missing firmware for logical ID {node['id']}: {firmware}")

    rows = ["logical_id\tboard\tphysical_id\tdata_len\tclass_id\tcapacity"]
    rows.extend("\t".join(str(node[key]) if node[key] is not None else "-"
                          for key in ("id", "board", "physical_id", "data_len", "class_id", "capacity"))
                for node in nodes)
    (out / "node_lengths.tsv").write_text("\n".join(rows) + "\n")
    profile = dict(mode=args.mode, node_count=count, p2_ntx=args.ntx,
                   max_data_len=args.max_data_len, class_count=args.class_count,
                   avg_percent=str(args.percent), total_data_len=sum(lengths),
                   nodes=nodes, commands=commands)
    (out / "experiment-build.json").write_text(json.dumps(profile, indent=2) + "\n")
    if args.nodes_file:
        # Preserve the exact manifest format checked by the existing mixed flash script.
        rows = [f"# nodes={count} p2_ntx={args.ntx}"]
        rows.extend(f"{node['id']}\t{node['board']}\t{node['physical_id']}" for node in nodes)
        (out / "mixed-build.tsv").write_text("\n".join(rows) + "\n")
    print(f"Built {count} firmware files in {out}")
    print("Use this directory as OUT when invoking the existing flash script.")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(f"Error: {error}")
