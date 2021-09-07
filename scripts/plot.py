#! /usr/bin/env python3

import csv
import sys
import argparse
import itertools
import fnmatch
import distutils.util
import os
from typing import Dict, Iterable, List, Sequence, Set, Tuple, Union
import matplotlib
import matplotlib.pyplot as plt


UnitKeyPairs = Dict[str, str]
PlotKeyPairs = Dict[str, bool]
AnyKeyPairs = Dict[str, Union[str, bool]]


def str_as_bool(s=None) -> bool:
    return False if not s else bool(distutils.util.strtobool(s))


def raise_empty_value() -> None:
    raise ValueError("Value cannot be empty")


class store_key_pairs(argparse.Action):
    def __init__(
        self,
        option_strings,
        dest=None,
        nargs=None,
        store_as=str,
        empty_val=None,
        **kwargs
    ) -> None:
        self.store_as = store_as
        self.empty_val = empty_val
        super().__init__(option_strings, dest, nargs, **kwargs)

    def __call__(self, parser, namespace, values, option_string) -> None:
        retval = {}
        for kv in values:
            k, _, v = kv.partition("=")
            if not k:
                raise argparse.ArgumentError(self, "Key cannot be empty")
            try:
                file, _, field = k.partition(":")
                if file and not field:
                    field = file
                    file = sys.stdin.name
                elif not file:
                    file = sys.stdin.name
                if file not in retval:
                    retval[file] = {}
                retval[file].update(
                    {field: self.store_as(v) if v else self.empty_val()}
                )
            except (ValueError, TypeError, argparse.ArgumentTypeError) as err:
                raise argparse.ArgumentError(
                    self, err.args[0] if err.args else "<empty>"
                )
        setattr(namespace, self.dest, retval)


class store_dpi(argparse.Action):
    def __init__(self, option_strings: Sequence[str], dest: str, **kwargs) -> None:
        super().__init__(option_strings, dest, **kwargs)

    def __call__(self, parser, namespace, values, option_string) -> None:
        try:
            retval = int(values) if values else self.default
            if retval <= 0:
                raise ValueError("DPI must be a positive integer")
            setattr(namespace, self.dest, retval)
        except (ValueError, TypeError, argparse.ArgumentTypeError) as err:
            raise argparse.ArgumentError(self, err.args[0] if err.args else "<empty>")


class store_size(argparse.Action):
    def __init__(self, option_strings: Sequence[str], dest: str, **kwargs) -> None:
        super().__init__(option_strings, dest, **kwargs)

    def __call__(self, parser, namespace, values, option_string) -> None:
        try:
            w, _, h = values.partition(",")
            retval = float(w), float(h)
            if retval[0] <= 0:
                raise ValueError("Width must be a positive decimal")
            if retval[1] <= 0:
                raise ValueError("Height must be a positive decimal")
            setattr(namespace, self.dest, retval)
        except (ValueError, TypeError, argparse.ArgumentTypeError) as err:
            raise argparse.ArgumentError(self, err.args[0] if err.args else "<empty>")


def read_from(path):
    if not path:
        return sys.stdin
    else:
        return open(path, "r")


def output_to(path):
    if not path:
        return sys.stdout
    else:
        return open(path, "wb")


def match_pattern(patterns: AnyKeyPairs, fieldnames: Sequence[str]) -> AnyKeyPairs:
    retval = {}
    for k, v in patterns.items():
        for m in fnmatch.filter(fieldnames, k):
            retval[m] = v
    return retval


def add_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "source_files",
        action="store",
        help="file to extract from (default: stdin)",
        nargs="*",
        type=str,
        default=[None],
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
        "-x",
        "--x-plots",
        action=store_key_pairs,
        store_as=str_as_bool,
        empty_val=str_as_bool,
        help="""column(s) to use as the x values as keys,
            with booleans as values that indicate whether to subtract
            the first x value of the corresponding column from the rest""",
        required=True,
        nargs="+",
        metavar="FILE:NAME=BOOL",
        default={},
        dest="x",
    )
    parser.add_argument(
        "-y",
        "--y-plots",
        action=store_key_pairs,
        store_as=str_as_bool,
        empty_val=str_as_bool,
        help="""column(s) to use as the y values as keys,
            with booleans as values that indicate whether to subtract
            the first y value of the corresponding column from the rest""",
        required=True,
        nargs="+",
        metavar="FILE:NAME=BOOL",
        default={},
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
    parser.add_argument(
        "-u",
        "--units",
        action=store_key_pairs,
        store_as=str,
        empty_val=raise_empty_value,
        help="plot units",
        required=False,
        nargs="*",
        metavar="FILE:NAME=UNIT",
        default={},
    )
    parser.add_argument(
        "-b",
        "--backend",
        action="store",
        type=str,
        help="backend to use when generating plot",
        required=False,
        choices=["agg", "pdf", "svg"],
        default="agg",
    )
    parser.add_argument(
        "--dpi",
        action=store_dpi,
        help="image DPI (only has an effect when backend is agg)",
        required=False,
        default=0,
    )
    parser.add_argument(
        "--scatter",
        action="store_true",
        help="use markers instead of a continuous line",
        required=False,
        default=False,
    )
    parser.add_argument(
        "-s",
        "--size",
        action=store_size,
        help="image width and height, in cm",
        metavar="WIDTH,HEIGHT",
        required=False,
        default=(),
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


def assert_key_pairs(kps, fieldnames) -> None:
    for k in kps:
        if k not in fieldnames:
            raise ValueError("'{}' is not a valid column".format(k))


def unique_units(plots: Iterable[str], units: UnitKeyPairs) -> bool:
    units = {v for k, v in units.items() if k in plots}
    return len(units) <= 1


def get_legend_prefix(source_files: Iterable[str], fname: str) -> str:
    if len(source_files) > 1:
        return os.path.basename(fname)
    return ""


def set_legend(line, x, y, prefix=None) -> None:
    if prefix:
        line.set_label("{}:{}({})".format(prefix, y, x))
    else:
        line.set_label("{}({})".format(y, x))


def get_label(plots: Dict[str, PlotKeyPairs], units: Dict[str, UnitKeyPairs]) -> str:
    BaseType = str
    CombType = Set[BaseType]
    LabelType = Union[BaseType, Set[BaseType]]

    def get_unique_label(plot: str, units: UnitKeyPairs) -> BaseType:
        if plot not in units:
            return plot
        return "{} ({})".format(plot, units[plot]) if units[plot] else plot

    def get_combined_units(plots: PlotKeyPairs, units: UnitKeyPairs) -> CombType:
        return {units[p] for p in plots if p in units} if units else {}

    def get_label_for_file(plots: PlotKeyPairs, units: UnitKeyPairs) -> LabelType:
        if len(plots) == 1:
            return get_unique_label(next(iter(plots)), units)
        return get_combined_units(plots, units)

    if len(plots) == 1:
        file, keypairs = next(iter(plots.items()))
        lbl = get_label_for_file(keypairs, units[file] if file in units else {})
        if isinstance(lbl, BaseType):
            return lbl
        return " / ".join(lbl)

    retval: CombType = set()
    for f, v in plots.items():
        if f in units:
            retval.update(get_combined_units(v, units[f]))
    return " / ".join(retval)


def get_title(args) -> str:
    if args.title:
        return args.title
    elif args.source_files and len(args.source_files) == 1:
        if args.source_files[0]:
            return os.path.basename(args.source_files[0])
        return sys.stdin.name
    return ",".join((os.path.basename(sf) for sf in args.source_files))


def cm2inch(size: Tuple[float, float]) -> Tuple[float, float]:
    inch = 2.54
    return size[0] / inch, size[1] / inch


def plots_compatible(x: PlotKeyPairs, y: PlotKeyPairs) -> bool:
    return len(x) == 1 or len(x) == len(y)


def main():
    parser = argparse.ArgumentParser(description="Generate plot from CSV file")
    add_arguments(parser)
    args = parser.parse_args()

    if len(args.source_files) == 1:
        for kp_args in args.x, args.y, args.units:
            if kp_args.get(sys.stdin.name):
                kp_args[args.source_files[0]] = kp_args[sys.stdin.name]
                del kp_args[sys.stdin.name]
    elif len(args.source_files) > 1:
        for kp_args in args.x, args.y, args.units:
            if kp_args.get(sys.stdin.name):
                raise ValueError(
                    (
                        "Multiple source files provided "
                        "but some key-value pairs "
                        "have no file specified"
                    )
                )

    for fname in args.source_files:
        found = False
        for kp_args in args.x, args.y, args.units:
            if fname in kp_args:
                found = True
        if not found:
            raise ValueError(
                "File {} is not referenced by any key-value pair".format(fname)
            )
    for kp_args in args.x, args.y, args.units:
        for file in kp_args:
            found = False
            if file in args.source_files:
                found = True
            if not found:
                raise ValueError("File {} is not a valid source file".format(file))

    marker_style = {}
    if args.scatter:
        marker_style["marker"] = "x"
        marker_style["markersize"] = 3
        marker_style["linestyle"] = ""

    matplotlib.use(args.backend)
    with plt.ioff():
        fig, ax = plt.subplots()
        if args.backend == "agg" and args.dpi:
            fig.set_dpi(args.dpi)
        if args.size:
            fig.set_size_inches(cm2inch(args.size))

        ax.minorticks_on()
        ax.set_title(get_title(args))
        ax.set_xlabel(get_label(args.x, args.units))
        ax.set_ylabel(get_label(args.y, args.units))
        ax.grid(which="major", axis="both", linestyle="dotted", alpha=0.5)

        for fname in args.source_files:
            with read_from(fname) as f:
                csvrdr = csv.DictReader(
                    row for row in f if not row.lstrip().startswith("#")
                )
                if not csvrdr.fieldnames:
                    raise AssertionError("Fieldnames cannot be empty or None")
                x_plots = args.x[f.name]
                y_plots = args.y[f.name]
                units = args.units[f.name]
                lg_prefix = get_legend_prefix(args.source_files, f.name)

                x_plots = match_pattern(x_plots, csvrdr.fieldnames)
                if not x_plots:
                    raise parser.error("Pattern matching for x plots: no matches found")
                assert_key_pairs(x_plots, csvrdr.fieldnames)
                y_plots = match_pattern(y_plots, csvrdr.fieldnames)
                if not y_plots:
                    raise parser.error("Pattern matching for y plots: no matches found")
                assert_key_pairs(y_plots, csvrdr.fieldnames)
                if not plots_compatible(x_plots, y_plots):
                    raise parser.error(
                        "x plot count does not match y plot count, found {} and {}".format(
                            len(x_plots), len(y_plots)
                        )
                    )
                units = match_pattern(units, csvrdr.fieldnames)
                if not units:
                    raise parser.error("Pattern matching for units: no matches found")
                if not unique_units(x_plots, units):
                    raise parser.error("units for x plots must be the same")

                converted = convert_input({**x_plots, **y_plots}, csvrdr)

                for x, y in itertools.zip_longest(
                    x_plots, y_plots, fillvalue=next(iter(x_plots))
                ):
                    (line,) = ax.plot(
                        converted[x], converted[y], linewidth=1, **marker_style
                    )
                    set_legend(line, x, y, lg_prefix)

        legend = ax.legend(
            bbox_to_anchor=(0.0, 1.1, 1.0, 0.1),
            loc="lower left",
            ncol=1,
            mode="expand",
            borderaxespad=0.0,
        )
        with output_to(args.output) as of:
            fig.savefig(of, bbox_extra_artists=(legend,), bbox_inches="tight")


if __name__ == "__main__":
    main()
