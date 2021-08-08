#!/usr/bin/env python3

import argparse
import csv
from io import TextIOWrapper
import sys
from typing import Dict, Iterable, List, Tuple


def output_to(path):
    return sys.stdout if not path else open(path, "w")


def add_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "-c",
        "--combine",
        action="store",
        help="files to combine",
        required=True,
        nargs="+",
        type=str,
        default=[],
        metavar="FILE",
        dest="sources",
    )
    parser.add_argument(
        "-o",
        "--output",
        action="store",
        help="destination file (default: stdout)",
        required=False,
        type=str,
        default=None,
    )


def read_file(f: TextIOWrapper) -> Tuple[Dict[str, List[str]], csv.reader]:
    def get_metadata(comments: Iterable[str]):
        return {row[0]: row[1:] for row in csv.reader(comments) if row}

    comments = []
    rest = []
    for row in f:
        if row.lstrip().rstrip().startswith("#"):
            comments.append(row.lstrip("#"))
        else:
            rest.append(row)
    return get_metadata(comments), csv.reader(rest)


def assert_units(units: Iterable[Iterable[str]]) -> None:
    first = next(iter(units))
    if not all(first == u for u in units):
        raise ValueError("Units must be the same")


def get_suffix(fname: str) -> str:
    return "[{}]".format(fname)


def combine_fields(fields: Iterable[str], suffix: str) -> List[str]:
    return ["{}{}".format(f, suffix) for f in fields]


def main():
    parser = argparse.ArgumentParser("Combine extracted executions into one file")
    add_args(parser)
    args = parser.parse_args()

    combined_meta = {"executions": []}
    combined_fields = []
    combined_rows = []
    for path in args.sources:
        with open(path, "r") as f:
            meta, rdr = read_file(f)
            suffix = get_suffix(f.name)
            combined_fields.extend(combine_fields(next(rdr), suffix))
            for key in ("group", "section", "devices"):
                if key not in combined_meta:
                    combined_meta[key] = set()
                combined_meta[key].update(meta[key])
            if "units" not in combined_meta:
                combined_meta["units"] = meta["units"]
            elif combined_meta["units"] != meta["units"]:
                raise ValueError("Units must be the same")
            combined_rows.append([row for row in rdr])

    max_len = max([len(rows) for rows in combined_rows])
    for rows in combined_rows:
        while len(rows) < max_len:
            rows.append(rows[-1])

    with output_to(args.output) as of:
        wrt = csv.writer(of)
        for k, v in combined_meta.items():
            wrt.writerow(("#" + k, *v))
        wrt.writerow(combined_fields)
        for rix in range(max_len):
            combined = []
            for rows in combined_rows:
                combined += rows[rix]
            wrt.writerow(combined)


if __name__ == "__main__":
    main()
