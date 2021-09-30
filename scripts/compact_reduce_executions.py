#!/usr/bin/env python3

import argparse
import json
import sys
from typing import Any, Dict, List, Union


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
        "--op",
        action="store",
        help="apply OPERATION to executions",
        metavar="OPERATION",
        required=True,
        type=str,
        choices=("sum", "max", "avg"),
    )
    return parser


Readings = Dict[str, Union[Dict[str, float], int]]
Exec = Dict[str, Union[Dict[str, str], float, List[Readings]]]


def reduce_max(execs: List[Any]) -> Exec:
    return {}


def reduce_avg(execs: List[Any]) -> Exec:
    return {}


def reduce_sum(execs: List[Any]) -> Exec:
    return {}


def process_execs(op: str, execs: List[Any]) -> Exec:
    if op == "sum":
        return reduce_sum(execs)
    if op == "avg":
        return reduce_avg(execs)
    if op == "max":
        return reduce_max(execs)
    raise AssertionError("Invalid operation type {}".format(op))


def main():
    parser = argparse.ArgumentParser(description="Reduce executions")
    args = add_arguments(parser).parse_args()
    with read_from(args.source_file) as f:
        json_in = json.load(f)
        for g in json_in["groups"]:
            for s in g["sections"]:
                s["executions"] = [process_execs(args.op, s["executions"])]
        with output_to(args.output) as of:
            json.dump(json_in, of)


if __name__ == "__main__":
    main()
