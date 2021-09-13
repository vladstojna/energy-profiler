#! /usr/bin/env python3

import fnmatch
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
    return sys.stdin if not path else open(path, "r")


def add_arguments(parser: argparse.ArgumentParser):
    def str_as_bool(s):
        return bool(distutils.util.strtobool(s))

    parser.add_argument(
        "source_files",
        action="store",
        help="file(s) to extract from (default: stdin)",
        nargs="*",
        type=str,
        default=[None],
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


def read_file(f) -> tuple[Iterable, Iterable]:
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


def print_format(fname: str, formatrow: list, pattern: str, value: bool) -> None:
    prefix = "{}:".format(fname) if fname else fname
    names = (
        "{}{}={}".format(prefix, m, value) for m in fnmatch.filter(formatrow, pattern)
    )
    print(" ".join(names))


def print_units(fname: str, formatrow: list, units: dict) -> None:
    def get_unit(text: str, units: dict) -> str:
        for k, v in units.items():
            if k in text:
                return v
        return "#"

    if not units:
        print()
    else:
        prefix = "{}:".format(fname) if fname else fname
        to_join = (
            "{}{}={}".format(prefix, k, v)
            for k, v in ((f, get_unit(f, units)) for f in formatrow)
        )
        print(" ".join(to_join))


def print_title(fname: str, meta: dict) -> None:
    if fname or meta.get("group") or meta.get("section") or meta.get("devices"):
        group = next(iter(meta["group"]))
        section = next(iter(meta["section"]))
        prefix = "{}:".format(fname) if fname else fname
        retval = "{}{}:{}".format(
            prefix,
            group if group else "<unnamed group>",
            section if section else "<unnamed section>",
        )
        if len(meta["devices"]) == 1:
            retval += ":{}".format(next(iter(meta["devices"])))
        print(retval)
    else:
        print("<untitled>")


def main():
    def is_candidate(w: str, s: str) -> bool:
        return w == s or w == "all"

    def get_filename(f) -> str:
        return f.name if f.name != sys.stdin.name else ""

    parser = argparse.ArgumentParser(description="Helper script to extract values")
    add_arguments(parser)
    args = parser.parse_args()
    if ("all" in args.what or "format" in args.what) and not args.pattern:
        parser.error("-w/--what all/format requires option -p/--pattern")

    for sf in args.source_files:
        with read_from(sf) as f:
            metadata, formatrow = read_file(f)
            fname = get_filename(f)
            assert fname or not args.source_files[0]
            if not fname and args.source_files[0]:
                raise AssertionError("file name must be exist if 1 or more files")
            if fname:
                print(fname)
            for what in args.what:
                if is_candidate(what, "format"):
                    print_format(fname, formatrow, args.pattern, args.value)
                if is_candidate(what, "units"):
                    units = get_units(metadata)
                    print_units(fname, formatrow, units)
                if is_candidate(what, "title"):
                    print_title(fname, metadata)


if __name__ == "__main__":
    main()
