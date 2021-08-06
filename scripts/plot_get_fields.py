#! /usr/bin/env python3

import fnmatch
import sys
import csv
import argparse
import distutils.util


def read_from(path):
    if not path:
        return sys.stdin
    else:
        return open(path, "r")


def str_as_bool(s):
    return bool(distutils.util.strtobool(s))


def add_arguments(parser: argparse.ArgumentParser):
    parser.add_argument(
        "source_file",
        action="store",
        help="file to extract from, or stdin if not present",
        nargs="?",
        type=str,
        default=None,
    )
    parser.add_argument(
        "-p",
        "--pattern",
        action="store",
        help="pattern and key of the key pair",
        type=str,
        required=True,
        default=None,
    )
    parser.add_argument(
        "-v",
        "--value",
        action="store",
        help="boolean value",
        required=False,
        type=str_as_bool,
        default=False,
    )


def main():
    parser = argparse.ArgumentParser(
        description="Helper script to extract format values as key-pairs"
    )
    add_arguments(parser)
    args = parser.parse_args()
    retval = {}
    with read_from(args.source_file) as f:
        for row in csv.reader(f):
            if not row[0].lstrip().startswith("#"):
                for m in fnmatch.filter(row, args.pattern):
                    retval[m] = args.value
                break
    for k, v in retval.items():
        print("{}={} ".format(k, v), end="")


if __name__ == "__main__":
    main()
