#! /usr/bin/env python3

import csv
import sys
import argparse
import itertools
import fnmatch
import distutils.util
import os
from typing import Any, Dict, Iterable, List, Optional, Sequence, Set, Tuple, Union
import matplotlib
import matplotlib.pyplot as plt


UnitKeyPairs = Dict[str, str]
PlotKeyPairs = Dict[str, bool]
AnyKeyPairs = Dict[str, Union[str, bool]]

UniqueLabel = Tuple[str, Optional[str]]
CombinedLabel = Set[str]
LabelType = Union[UniqueLabel, CombinedLabel]


def str_as_bool(s=None) -> bool:
    return False if not s else bool(distutils.util.strtobool(s))


def raise_empty_value() -> None:
    raise ValueError("Value cannot be empty")


class store_key_pairs(argparse.Action):
    default_file = "default"

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
                    file = self.default_file
                elif not file:
                    file = self.default_file
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


def match_pattern(patterns: Dict[str, Any], names: Sequence[str]) -> Dict[str, Any]:
    retval = {}
    for k, v in patterns.items():
        for m in fnmatch.filter(names, k):
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
        line.set_label("[{}] {}({})".format(prefix, y, x))
    else:
        line.set_label("{}({})".format(y, x))


def get_label(plots: PlotKeyPairs, units: UnitKeyPairs) -> LabelType:
    def get_unique_label(plot: str, units: UnitKeyPairs) -> UniqueLabel:
        return (plot, units.get(plot))

    def get_combined_units(plots: PlotKeyPairs, units: UnitKeyPairs) -> CombinedLabel:
        return {units[p] for p in plots if p in units} if units else {}

    if len(plots) == 1:
        return get_unique_label(next(iter(plots)), units)
    return get_combined_units(plots, units)


def combine_labels(labels: List[LabelType]) -> str:
    def label_to_str(label: LabelType) -> str:
        if isinstance(label, tuple):
            return "{} ({})".format(label[0], label[1]) if label[1] else label[0]
        if isinstance(label, set):
            return " / ".join(label)
        raise TypeError("Invalid label type encountered")

    if len(labels) == 1:
        return label_to_str(labels[0])

    fully_combined: CombinedLabel = set()
    for lbl in labels:
        if isinstance(lbl, tuple):
            if lbl[1]:
                fully_combined.add(lbl[1])
        elif isinstance(lbl, set):
            fully_combined.update(lbl)
        else:
            raise TypeError("Invalid label type encountered")
    return label_to_str(fully_combined)


def get_title(args) -> str:
    if args.title:
        return args.title
    elif args.source_files and len(args.source_files) == 1:
        if args.source_files[0]:
            return os.path.basename(args.source_files[0])
        return store_key_pairs.default_file
    return ",".join((os.path.basename(sf) for sf in args.source_files))


def cm2inch(size: Tuple[float, float]) -> Tuple[float, float]:
    inch = 2.54
    return size[0] / inch, size[1] / inch


def plots_compatible(x: PlotKeyPairs, y: PlotKeyPairs) -> bool:
    return len(x) == 1 or len(x) == len(y)


def substitute_default_file(args) -> None:
    for kp_args in args.x, args.y, args.units:
        if kp_args.get(store_key_pairs.default_file):
            kp_args[args.source_files[0]] = kp_args[store_key_pairs.default_file]
            del kp_args[store_key_pairs.default_file]


def assert_files_provided(args) -> None:
    for kp_args in args.x, args.y, args.units:
        if kp_args.get(store_key_pairs.default_file):
            raise ValueError(
                (
                    "Multiple source files provided "
                    "but some key-value pairs "
                    "have no file specified"
                )
            )


def pattern_match_files(args) -> None:
    args.x = match_pattern(args.x, args.source_files)
    args.y = match_pattern(args.y, args.source_files)
    args.units = match_pattern(args.units, args.source_files)


def main():
    parser = argparse.ArgumentParser(description="Generate plot from CSV file")
    add_arguments(parser)
    args = parser.parse_args()

    if len(args.source_files) == 1:
        substitute_default_file(args)
    elif len(args.source_files) > 1:
        assert_files_provided(args)
    pattern_match_files(args)

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

    style = {}
    if args.scatter:
        style["marker"] = "."
        style["markersize"] = 2
        style["linestyle"] = "dotted"
        style["linewidth"] = 0.7
    else:
        style["linewidth"] = 1

    matplotlib.use(args.backend)
    with plt.ioff():
        fig, ax = plt.subplots()
        if args.backend == "agg" and args.dpi:
            fig.set_dpi(args.dpi)
        if args.size:
            fig.set_size_inches(cm2inch(args.size))

        ax.minorticks_on()
        ax.set_title(get_title(args))
        ax.grid(which="major", axis="both", linestyle="dotted", alpha=0.5)

        labels: Tuple[List[LabelType], List[LabelType]] = ([], [])
        for fname in args.source_files:
            with read_from(fname) as f:
                csvrdr = csv.DictReader(
                    row for row in f if not row.lstrip().startswith("#")
                )
                if not csvrdr.fieldnames:
                    raise AssertionError("Fieldnames cannot be empty or None")
                x_plots = args.x[f.name]
                y_plots = args.y[f.name]
                units = args.units.get(f.name, {})
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
                if not unique_units(x_plots, units):
                    raise parser.error("units for x plots must be the same")

                converted = convert_input({**x_plots, **y_plots}, csvrdr)

                for x, y in itertools.zip_longest(
                    x_plots, y_plots, fillvalue=next(iter(x_plots))
                ):
                    (line,) = ax.plot(converted[x], converted[y], **style)
                    set_legend(line, x, y, lg_prefix)

                labels[0].append(get_label(x_plots, units))
                labels[1].append(get_label(y_plots, units))

        ax.set_xlabel(combine_labels(labels[0]))
        ax.set_ylabel(combine_labels(labels[1]))
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
