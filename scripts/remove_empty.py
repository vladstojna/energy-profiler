#!/usr/bin/env python3

import json
import sys
import argparse
from typing import Any


def read_from(path: str) -> Any:
    return sys.stdin if not path else open(path, "r")


def output_to(path: str) -> Any:
    return sys.stdout if not path else open(path, "w")


def add_arguments(parser: argparse.ArgumentParser) -> argparse.ArgumentParser:
    def _add_bool_flag(arg: str, obj_type: str) -> None:
        parser.add_argument(
            arg,
            action="store_true",
            help=f"keep {obj_type} objects even if empty",
            required=False,
            default=False,
        )

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
    _add_bool_flag("--keep-group", "group")
    _add_bool_flag("--keep-section", "section")
    _add_bool_flag("--keep-location", "location")
    return parser


def main():
    targets = ("cpu", "gpu")

    parser = argparse.ArgumentParser(
        description="Remove empty entries from profiler output"
    )
    args = add_arguments(parser).parse_args()
    with read_from(args.source_file) as f:
        json_in = json.load(f)


if __name__ == "__main__":
    main()
