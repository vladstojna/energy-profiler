#! /usr/bin/env python3

import csv
import sys
import argparse
import itertools
import distutils.util
import matplotlib
import matplotlib.pyplot as plt


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
                raise argparse.ArgumentError(self, "Key cannot be empty")
            if len(rest) > 1:
                raise argparse.ArgumentError(self, "Only one value is permitted")
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
        "--x-plots",
        action=store_bool_key_pairs,
        help="""column(s) to use as the x values as keys,
            with booleans as values that indicate whether to subtract
            the first x value of the corresponding column from the rest""",
        required=True,
        nargs="+",
        metavar="NAME=BOOL",
        default=None,
        dest="x",
    )
    parser.add_argument(
        "-y",
        "--y-plots",
        action=store_bool_key_pairs,
        help="""column(s) to use as the y values as keys,
            with booleans as values that indicate whether to subtract
            the first y value of the corresponding column from the rest""",
        required=True,
        nargs="+",
        metavar="NAME=BOOL",
        default=None,
        dest="y",
    )
    parser.add_argument(
        "-t",
        "--title",
        action="store",
        help="plot title",
        required=False,
        type=str,
        default=None,
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


def assert_key_pairs(kps, fieldnames):
    for k in kps:
        if k not in fieldnames:
            raise ValueError("'{}' is not a valid column".format(k))


def set_legend(line, x, y):
    line.set_label("{}({})".format(y, x))


def main():
    parser = argparse.ArgumentParser(description="Generate plot from CSV file")
    add_arguments(parser)
    args = parser.parse_args()
    if len(args.x) > 1 and len(args.y) > 1 and len(args.x) != len(args.y):
        raise ValueError("if more than one X, then number must match Y count")
    with read_from(args.source_file) as f, output_to(args.output) as o:
        csvrdr = csv.DictReader(row for row in f if not row.startswith("#"))
        assert_key_pairs(args.x, csvrdr.fieldnames)
        assert_key_pairs(args.y, csvrdr.fieldnames)
        converted = convert_input({**args.x, **args.y}, csvrdr)

        matplotlib.use("agg")
        with plt.ioff():
            fig, ax = plt.subplots()
            ax.set_title(args.title if args.title else f.name)
            ax.minorticks_on()
            for x, y in itertools.zip_longest(
                args.x, args.y, fillvalue=next(iter(args.x))
            ):
                (line,) = ax.plot(converted[x], converted[y])
                set_legend(line, x, y)
            legend = ax.legend(
                bbox_to_anchor=(0.0, 1.1, 1.0, 0.1),
                loc="lower left",
                ncol=1,
                mode="expand",
                borderaxespad=0.0,
            )
            plt.grid(which="major", axis="both", linestyle="dotted", alpha=0.5)
            plt.savefig(o, bbox_extra_artists=(legend,), bbox_inches="tight")


if __name__ == "__main__":
    main()
