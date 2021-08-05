#! /usr/bin/env python3

import csv
import sys
import argparse
import distutils.util
import matplotlib
import matplotlib.pyplot as plt


class bool_key_pair:
    def __init__(self, key: str, value: bool) -> None:
        self.key = key
        self.sub = value

    def __str__(self) -> str:
        return "key={}, sub={}".format(self.key, self.sub)


class store_bool_key_pair(argparse.Action):
    def __init__(self, option_strings, **kwargs) -> None:
        super().__init__(option_strings, **kwargs)

    def __call__(
        self,
        parser,
        namespace,
        values,
        option_string,
    ) -> None:
        k, *rest = values.split("=")
        if not k:
            raise ValueError("Key cannot be empty")
        if len(rest) > 1:
            raise ValueError("Only one value is permitted")
        retval = bool_key_pair(
            k, False if not rest else bool(distutils.util.strtobool(rest[0]))
        )
        setattr(namespace, self.dest, retval)


class store_bool_key_pairs(argparse.Action):
    def __init__(self, option_strings, dest=None, nargs=None, **kwargs) -> None:
        super().__init__(option_strings, dest, nargs, **kwargs)

    def __call__(
        self,
        parser,
        namespace,
        values,
        option_string=None,
    ) -> None:
        retval = {}
        for kv in values:
            k, *rest = kv.split("=")
            if not k:
                raise ValueError("Key cannot be empty")
            if len(rest) > 1:
                raise ValueError("Only one value is permitted")
            retval[k] = False if not rest else bool(distutils.util.strtobool(rest[0]))
        setattr(namespace, self.dest, retval)


def read_from(path):
    if not path:
        return sys.stdin
    else:
        return open(path, "r")


def output_to(path):
    if not path:
        return sys.stdout
    else:
        return open(path, "w")


def add_arguments(parser):
    parser.add_argument(
        "source_file",
        action="store",
        help="file to extract from",
        nargs="?",
        type=str,
        default=None,
    )
    parser.add_argument(
        "-o",
        "--output",
        action="store",
        help="destination file or stdout if not present",
        required=False,
        type=str,
        default=None,
    )
    parser.add_argument(
        "-x",
        "--x-axis",
        action=store_bool_key_pair,
        help="""column to use as the x-axis as key,
            with a boolean as value that indicates whether
            to subtract the first x value from the rest""",
        required=True,
        metavar="NAME=BOOL",
        default=None,
        dest="x",
    )
    parser.add_argument(
        "-y",
        "--y-axis",
        action=store_bool_key_pairs,
        help="""column(s) to use as the y-axis(es) as keys,
            with booleans as values that indicate whether to subtract
            the first y value of the corresponding column from the rest""",
        required=True,
        nargs="+",
        metavar="NAME=BOOL",
        default=None,
        dest="y",
    )


def convert_input(fields, data) -> dict:
    retval = {f: [] for f in fields}
    first_row = None
    for row in data:
        if not first_row:
            first_row = row
        for k, v in row.items():
            if k in fields:
                retval[k].append(
                    float(v) - float(first_row[k]) if fields[k] else float(v)
                )
    return retval


def main():
    parser = argparse.ArgumentParser(description="Generate plot from CSV file")
    add_arguments(parser)
    args = parser.parse_args()
    with read_from(args.source_file) as f, output_to(args.output) as o:
        csvrdr = csv.DictReader(row for row in f if not row.startswith("#"))
        if args.x.key not in csvrdr.fieldnames:
            raise ValueError("'{}' is not a valid column".format(args.x.key))
        for y in args.y:
            if y not in csvrdr.fieldnames:
                raise ValueError("'{}' is not a valid column".format(y))
        converted = convert_input({args.x.key: args.x.sub, **args.y}, csvrdr)

        matplotlib.use("agg")
        with plt.ioff():
            fig, ax = plt.subplots()
            ax.set_title(f.name)
            ax.set_xlabel(args.x.key)
            for y in args.y:
                (line,) = ax.plot(converted[args.x.key], converted[y])
                line.set_label(y)
            ax.legend()
            plt.savefig(o)


if __name__ == "__main__":
    main()
