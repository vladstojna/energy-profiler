#!/usr/bin/env python3

import argparse
import json
import sys
from typing import Any


def log(*args: Any) -> None:
    print("{}:".format(sys.argv[0]), *args, file=sys.stderr)


def read_from(path: str) -> Any:
    return sys.stdin if not path else open(path, "r")


def output_to(path: str) -> Any:
    return sys.stdout if not path else open(path, "w")


def add_arguments(parser: argparse.ArgumentParser) -> argparse.ArgumentParser:
    parser.add_argument(
        "source_file",
        action="store",
        help="file to extract from (default: stdin)",
        nargs="?",
        type=str,
        default=None,
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
    parser.add_argument(
        "-t",
        "--threshold",
        action="store",
        help="remove executions with duration in seconds below VALUE",
        metavar="VALUE",
        required=True,
        type=float,
    )
    return parser


def main():
    parser = argparse.ArgumentParser(
        description="Remove executions with duration./co below the time threshold"
    )
    args = add_arguments(parser).parse_args()
    with read_from(args.source_file) as f:
        json_in = json.load(f)
        for g in json_in["groups"]:
            for s in g["sections"]:
                e_to_keep = []
                for eix, e in enumerate(s["executions"]):
                    if float(e["time"]) >= args.threshold:
                        e_to_keep.append(e)
                    else:
                        log("remove {}:{}:{}".format(g["label"], s["label"], eix))
                s["executions"] = e_to_keep
        with output_to(args.output) as of:
            json.dump(json_in, of)


if __name__ == "__main__":
    main()
