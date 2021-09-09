#! /usr/bin/env python3

import fnmatch
from io import TextIOWrapper
import sys
import csv
import argparse
import distutils.util
from typing import Iterable, TextIO, Union


class argset_or_all(argparse.Action):
    def __init__(self, option_strings, dest=None, nargs=None, **kwargs) -> None:
        super().__init__(option_strings, dest, nargs, **kwargs)

    def __call__(self, parser, namespace, values, option_string) -> None:
        retval = {"all"} if "all" in values else {v for v in values}
        setattr(namespace, self.dest, retval)


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
        "-w",
        "--what",
        action=argset_or_all,
        help="which fields to extract",
        required=True,
        nargs="+",
        type=str,
        choices=["format", "units", "title", "all"],
    )
    parser.add_argument(
        "-p",
        "--pattern",
        action="store",
        help="pattern and key of the key pair, used with -w/--what format",
        type=str,
        required=False,
        default=None,
    )
    parser.add_argument(
        "-v",
        "--value",
        action="store",
        help="boolean value, used with -w/--what format",
        required=False,
        type=str_as_bool,
        default=False,
    )


def read_file(f: Union[TextIO, TextIOWrapper]) -> tuple[Iterable, Iterable]:
    def get_metadata(comments: list):
        return {c[0]: c[1:] for c in csv.reader(comments) if c}

    comments = []
    formatrow = None
    for row in f:
        if row.lstrip().rstrip().startswith("#"):
            comments.append(row.lstrip("#"))
        else:
            formatrow = next(iter(csv.reader([row])))
            break
    assert formatrow is not None
    return get_metadata(comments), formatrow


def get_units(meta: dict) -> dict:
    retval = {}
    if not meta.get("units"):
        return retval
    for unit in meta["units"]:
        utype, usym = unit.split("=", 1)
        retval[utype] = usym
    return retval


def print_format(formatrow: list, pattern: str, value: bool) -> None:
    print(
        " ".join("{}={}".format(m, value) for m in fnmatch.filter(formatrow, pattern))
    )


def print_units(formatrow: list, units: dict) -> None:
    def get_unit(text: str, units: dict) -> str:
        for k, v in units.items():
            if k in text:
                return v
        return "#"

    if not units:
        print()
    else:
        dic = {}
        for f in formatrow:
            dic[f] = get_unit(f, units)
        print(" ".join("{}={}".format(k, v) for k, v in dic.items()))


def print_title(meta: dict) -> None:
    if not meta.get("group") or not meta.get("section") or not meta.get("devices"):
        print("<untitled>")
    else:
        retval = "{}, {}".format(next(iter(meta["group"])), next(iter(meta["section"])))
        if len(meta["devices"]) == 1:
            retval += ", {}".format(next(iter(meta["devices"])))
        print(retval)


def main():
    parser = argparse.ArgumentParser(description="Helper script to extract values")
    add_arguments(parser)
    args = parser.parse_args()
    if (args.what == "all" or args.what == "format") and not args.pattern:
        parser.error("-w/--what all/format requires option -p/--pattern")

    with read_from(args.source_file) as f:
        metadata, formatrow = read_file(f)
        units = get_units(metadata)
        for what in args.what:
            if what == "all":
                print_format(formatrow, args.pattern, args.value)
                print_units(formatrow, units)
                print_title(metadata)
            elif what == "format":
                print_format(formatrow, args.pattern, args.value)
            elif what == "units":
                print_units(formatrow, units)
            elif what == "title":
                print_title(metadata)
            else:
                assert False


if __name__ == "__main__":
    main()
