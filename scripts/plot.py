#!/usr/bin/env python3

import csv
import copy
import sys
import argparse
import itertools
import fnmatch
import pickle
import distutils.util
import os
from typing import (
    Any,
    Callable,
    Dict,
    Iterable,
    List,
    Optional,
    Sequence,
    Set,
    Tuple,
    Union,
)
import matplotlib
import matplotlib.pyplot as plt


PlotKeyPair = Tuple[str, bool]
AnyKeyPair = Tuple[Any, Any]

UnitKeyPairs = Dict[str, str]
PlotKeyPairs = List[PlotKeyPair]
AnyKeyPairs = List[AnyKeyPair]

ConstValue = Union[int, float]

UniqueLabel = Tuple[str, Optional[str]]
CombinedLabel = Set[str]
LabelType = Union[UniqueLabel, CombinedLabel]


def log(*args: Any) -> None:
    print("{}:".format(sys.argv[0]), *args, file=sys.stderr)


def str_as_bool(s=None) -> bool:
    return False if not s else bool(distutils.util.strtobool(s))


def raise_empty_value() -> None:
    raise ValueError("Value cannot be empty")


def generate_constant_series(value: ConstValue, count: int) -> Iterable[ConstValue]:
    return (value,) * count


class store_keypairs(argparse.Action):
    default_file = "default"

    def store_file(
        self, lhs: str, rhs: str, files: Dict[str, Any], create_container: Callable
    ) -> Tuple[str, str]:
        if lhs and not rhs:
            lhs, rhs = self.default_file, lhs
        elif not lhs:
            lhs = self.default_file
        if lhs not in files:
            inserted = files[lhs] = create_container()
        else:
            inserted = files[lhs]
        return lhs, rhs, inserted

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
        raise NotImplementedError("__call__ not implemented")


class store_keypairs_unique(store_keypairs):
    def __call__(self, parser, namespace, values, option_string) -> None:
        retval = {}
        for kv in values:
            k, _, v = kv.partition("=")
            if not k:
                raise argparse.ArgumentError(self, "Key cannot be empty")
            try:
                file, _, field = k.partition(":")
                file, field, ins = self.store_file(file, field, retval, lambda: {})
                ins.update({field: self.store_as(v) if v else self.empty_val()})
            except (ValueError, TypeError, argparse.ArgumentTypeError) as err:
                raise argparse.ArgumentError(
                    self, err.args[0] if err.args else "<empty>"
                )
        setattr(namespace, self.dest, retval)


class store_keypairs_or_scalar(store_keypairs):
    def __call__(self, parser, namespace, values, option_string) -> None:
        def inf_or_nan(value: float) -> bool:
            return not (float("-inf") < value < float("inf"))

        def append_field(field: str, current: List[str]) -> None:
            current.append((field, self.store_as(value) if value else self.empty_val()))

        retval = {}
        try:
            for fkv in values:
                file, _, kv = fkv.partition(":")
                file, kv, ins = self.store_file(file, kv, retval, lambda: [])
                field, _, value = kv.partition("=")
                if not field:
                    raise argparse.ArgumentError(self, "Key cannot be empty")
                # if value is not empty, assume field name
                # otherwise, try and parse a constant
                if value:
                    append_field(field, ins)
                else:
                    try:
                        const = float(field)
                        if inf_or_nan(const):
                            raise ValueError("Constant must be a real value")
                        ins.append((generate_constant_series, const))
                    # assume it is a field name and append to list
                    except ValueError:
                        append_field(field, ins)
        except (ValueError, TypeError, argparse.ArgumentTypeError) as err:
            raise argparse.ArgumentError(self, err.args[0] if err.args else "<empty>")
        setattr(namespace, self.dest, retval)


class store_marker_style(argparse.Action):
    _choices = ("const", "nonconst")
    default = {"const": "x", "nonconst": "."}
    default_str = ",".join("{}={}".format(k, v) for k, v in default.items())

    def __init__(self, option_strings: Sequence[str], dest: str, **kwargs) -> None:
        super().__init__(option_strings, dest, **kwargs)

    def __call__(self, parser, namespace, values, option_string) -> None:
        try:
            retval = {}
            for kv in values:
                k, sep, v = kv.partition("=")
                if not sep:
                    raise ValueError("Format in key=value")
                if not k:
                    raise ValueError("Key cannot be empty")
                if k not in self._choices:
                    raise ValueError(
                        "Invalid key: {} not in {}".format(k, ",".join(self._choices))
                    )
                retval[k] = v
            setattr(namespace, self.dest, retval)
        except (ValueError, TypeError, argparse.ArgumentTypeError) as err:
            raise argparse.ArgumentError(self, err.args[0] if err.args else "<empty>")


def read_from(path: Optional[str]):
    return sys.stdin if not path else open(path, "r")


def output_to(path: Optional[str]):
    return sys.stdout.buffer if not path else open(path, "wb")


def match_pattern(
    patterns: Iterable,
    names: Sequence[str],
    use_fnmatch: Callable = lambda _: True,
    no_matches: Callable = lambda _: None,
) -> Union[List[AnyKeyPair], Dict]:
    def _match_dict(pats, names, func):
        def _add_or_update(cont, match, val):
            if match in cont:
                if isinstance(val, list):
                    cont[match] += val
                elif isinstance(val, dict):
                    cont[match].update(val)
                else:
                    cont[match] = val
            else:
                if isinstance(val, list) or isinstance(val, dict):
                    cont[match] = copy.deepcopy(val)
                else:
                    cont[match] = val

        retval = {}
        for k, v in pats.items():
            if func(k):
                matches = fnmatch.filter(names, k)
                if not matches:
                    no_matches(k, names)
                else:
                    for m in matches:
                        _add_or_update(retval, m, v)
            else:
                retval[k] = v
        return retval

    def _match_list(pats, names, func):
        def _add_or_update(cont, match, val):
            idx, _ = next(
                filter(lambda elem: elem[1][0] == match, enumerate(cont)), (None, None)
            )
            if idx is not None:
                cont[idx] = (match, val)
            else:
                cont.append((match, val))

        retval = []
        for k, v in pats:
            if func(k):
                matches = fnmatch.filter(names, k)
                if not matches:
                    no_matches(k, names)
                else:
                    for m in matches:
                        _add_or_update(retval, m, v)
            else:
                _add_or_update(retval, k, v)
        return retval

    if isinstance(patterns, dict):
        return _match_dict(patterns, names, use_fnmatch)
    elif isinstance(patterns, list):
        return _match_list(patterns, names, use_fnmatch)
    else:
        raise AssertionError("match_pattern invalid type")


def add_arguments(parser: argparse.ArgumentParser) -> None:
    def positive_int_or_float(s: str) -> Union[int, float]:
        try:
            val = int(s)
            if val <= 0:
                raise argparse.ArgumentTypeError("value must be positive")
            return val
        except ValueError:
            try:
                val = float(s)
                if val <= 0:
                    raise argparse.ArgumentTypeError("value must be positive")
            except ValueError as err:
                raise argparse.ArgumentTypeError(
                    err.args[0] if len(err.args) else "could not convert value to float"
                )

    default_marker_size = 3

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
        action=store_keypairs_or_scalar,
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
        "--xl",
        "--x-label",
        action="store",
        help="X axis label",
        type=str,
        required=False,
        default=None,
        dest="xlabel",
    )
    parser.add_argument(
        "-y",
        "--y-plots",
        action=store_keypairs_or_scalar,
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
        "--yl",
        "--y-label",
        action="store",
        help="Y axis label",
        type=str,
        required=False,
        default=None,
        dest="ylabel",
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
        "--legends",
        action="store",
        help="legend associated with each plot",
        required=False,
        nargs="+",
        default=[],
    )
    parser.add_argument(
        "--no-legend",
        action="store_true",
        help="disable plot legend",
        required=False,
        default=False,
    )
    parser.add_argument(
        "-u",
        "--units",
        action=store_keypairs_unique,
        store_as=str,
        empty_val=raise_empty_value,
        help="plot units",
        required=False,
        nargs="*",
        metavar="FILE:NAME=UNIT",
        default={},
    )
    parser.add_argument(
        "--scatter",
        action="store",
        help="""use markers with size SIZE instead
            of a continuous line (default: {})""".format(
            default_marker_size
        ),
        required=False,
        type=positive_int_or_float,
        nargs="?",
        metavar="SIZE",
        default=None,
        const=default_marker_size,
    )
    parser.add_argument(
        "--marker-line",
        action="store",
        help="""draw a line between markers;
            no effect if --scatter is not provided (default: all)""",
        choices=("all", "const", "nonconst", "none"),
        default="all",
        required=False,
        type=str,
    )
    parser.add_argument(
        "--marker-style",
        action=store_marker_style,
        help="""set marker style as STYLE for SERIES series
            (default: {})""".format(
            store_marker_style.default_str
        ),
        required=False,
        nargs="+",
        metavar="SERIES=STYLE",
        default=store_marker_style.default,
    )


def assert_key_pairs(kps, fieldnames) -> None:
    for fld, _ in kps:
        if not callable(fld) and fld not in fieldnames:
            raise ValueError("'{}' is not a valid column".format(fld))


def unique_units(plots: Iterable[str], units: UnitKeyPairs) -> bool:
    units = {v for k, v in units.items() if k in plots}
    return len(units) <= 1


def get_legend_prefix(source_files: Iterable[str], fname: str) -> str:
    if len(source_files) > 1:
        return os.path.basename(fname)
    return ""


def generate_legend(x, y, prefix=None) -> str:
    if prefix:
        return "[{}] {}({})".format(prefix, y, x)
    else:
        return "{}({})".format(y, x)


def get_label(plots: PlotKeyPairs, units: UnitKeyPairs) -> LabelType:
    def get_unique_label(plot: PlotKeyPair, units: UnitKeyPairs) -> UniqueLabel:
        return (plot[0], units.get(plot[0])) if not callable(plot[0]) else (None, None)

    def get_combined_units(plots: PlotKeyPairs, units: UnitKeyPairs) -> CombinedLabel:
        return {units[p] for p, _ in plots if p in units} if units else set()

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
        return store_keypairs.default_file
    return ",".join((os.path.basename(sf) for sf in args.source_files))


def cm2inch(size: Tuple[float, float]) -> Tuple[float, float]:
    inch = 2.54
    return size[0] / inch, size[1] / inch


def plots_compatible(x: PlotKeyPairs, y: PlotKeyPairs) -> bool:
    return len(x) == 1 or len(x) == len(y)


def substitute_default_file(args) -> None:
    for kp_args in args.x, args.y, args.units:
        if kp_args.get(store_keypairs.default_file):
            kp_args[args.source_files[0]] = kp_args[store_keypairs.default_file]
            del kp_args[store_keypairs.default_file]


def assert_files_provided(args) -> None:
    for kp_args in args.x, args.y, args.units:
        if kp_args.get(store_keypairs.default_file):
            raise ValueError(
                (
                    "Multiple source files provided "
                    "but some key-value pairs "
                    "have no file specified"
                )
            )


def pattern_match_files(args) -> None:
    def _raise(pat, names):
        raise ValueError(
            "no matches found for file '{}' in {}".format(pat, ", ".join(names))
        )

    args.x = match_pattern(args.x, args.source_files, no_matches=_raise)
    args.y = match_pattern(args.y, args.source_files, no_matches=_raise)
    args.units = match_pattern(args.units, args.source_files, no_matches=_raise)


def pattern_matching_error(
    parser: argparse.ArgumentParser, dim: str, fname: str
) -> None:
    raise parser.error(
        "Pattern matching for {} plots: no matches found in {}".format(dim, fname)
    )


def convert_input(
    x_fields: Iterable[AnyKeyPairs], y_fields: Iterable[AnyKeyPairs], data: Iterable
) -> List:
    def _find_field(
        fields: Iterable[AnyKeyPairs], data: List[AnyKeyPairs], key: Any
    ) -> Tuple:
        for (f, v), (_, d) in zip(fields, data):
            if not callable(f) and f == key:
                return v, d
        return None, None

    def _process_row(
        x_fields, x_ret, y_fields, y_ret, first: Any, val: Any, key: str
    ) -> None:
        x_fieldval, x_datalist = _find_field(x_fields, x_ret, key)
        y_fieldval, y_datalist = _find_field(y_fields, y_ret, key)
        if x_fieldval is not None:
            x_datalist.append(float(val) - float(first) if x_fieldval else float(val))
        if y_fieldval is not None:
            y_datalist.append(float(val) - float(first) if y_fieldval else float(val))

    x_ret = [(f, [] if not callable(f) else v) for f, v in x_fields]
    y_ret = [(f, [] if not callable(f) else v) for f, v in y_fields]

    first_row = next(iter(data), None)
    if first_row is None:
        raise AssertionError("No rows in CSV file")
    for k, v in first_row.items():
        _process_row(x_fields, x_ret, y_fields, y_ret, v, v, k)
    for row in data:
        for k, v in row.items():
            _process_row(x_fields, x_ret, y_fields, y_ret, first_row[k], v, k)
    return x_ret, y_ret


def get_line_marker_style(
    scatter: Optional[Union[float, int]],
    marker_line: str,
    marker_style: Dict[str, str],
    x,
    y,
) -> Dict:
    def _create_dict(ls: str, m: str) -> Dict:
        return {"linestyle": ls, "marker": m}

    def _line(m: str) -> Dict:
        return _create_dict("dotted", m)

    def _no_line(m: str) -> Dict:
        return _create_dict("", m)

    def _marker(mstyle, x, y) -> str:
        if callable(x) or callable(y):
            return mstyle["const"]
        return mstyle["nonconst"]

    if scatter is None:
        return {}
    if marker_line == "none":
        return _no_line(_marker(marker_style, x, y))
    if marker_line == "all":
        return _line(_marker(marker_style, x, y))
    if marker_line == "const":
        if callable(x) or callable(y):
            return _line(marker_style[marker_line])
        return _no_line(marker_style[marker_line])
    if marker_line == "nonconst":
        if callable(x) or callable(y):
            return _no_line(marker_style[marker_line])
        return _line(marker_style[marker_line])
    raise AssertionError("invalid marker_line value")


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

    style_common = (
        {"markersize": args.scatter, "linewidth": 0.7}
        if args.scatter is not None
        else {"linewidth": 1}
    )

    with plt.ioff():
        fig, ax = plt.subplots()
        ax.minorticks_on()
        ax.set_title(get_title(args))
        ax.grid(which="both", axis="both", linestyle="dotted", alpha=0.2)

        labels: Tuple[List[LabelType], List[LabelType]] = ([], [])
        legend_iter = iter(args.legends)
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
                no_match = lambda k, _: log(
                    "column {} not found in {}".format(k, f.name)
                )

                x_plots: AnyKeyPairs = match_pattern(
                    x_plots,
                    csvrdr.fieldnames,
                    use_fnmatch=lambda x: not callable(x),
                    no_matches=no_match,
                )
                if not x_plots:
                    pattern_matching_error(parser, "x", f.name)
                assert_key_pairs(x_plots, csvrdr.fieldnames)
                y_plots: AnyKeyPairs = match_pattern(
                    y_plots,
                    csvrdr.fieldnames,
                    use_fnmatch=lambda x: not callable(x),
                    no_matches=no_match,
                )
                if not y_plots:
                    pattern_matching_error(parser, "y", f.name)
                assert_key_pairs(y_plots, csvrdr.fieldnames)
                if not plots_compatible(x_plots, y_plots):
                    raise parser.error(
                        "x plot count does not match y plot count, found {} and {}".format(
                            len(x_plots), len(y_plots)
                        )
                    )
                units: UnitKeyPairs = match_pattern(units, csvrdr.fieldnames)
                if not unique_units(x_plots, units):
                    raise parser.error("units for x plots must be the same")

                x_conv, y_conv = convert_input(x_plots, y_plots, csvrdr)
                for (xf, xval), (yf, yval) in itertools.zip_longest(
                    x_conv, y_conv, fillvalue=next(iter(x_conv))
                ):
                    get_plot_values = (
                        lambda f1, v1, f2, v2: f1(v1, 1 if callable(f2) else len(v2))
                        if callable(f1)
                        else v1
                    )
                    (line,) = ax.plot(
                        get_plot_values(xf, xval, yf, yval),
                        get_plot_values(yf, yval, xf, xval),
                        **style_common,
                        **get_line_marker_style(
                            args.scatter, args.marker_line, args.marker_style, xf, yf
                        ),
                    )
                    if not args.no_legend:
                        next_legend = next(legend_iter, None)
                        line.set_label(
                            next_legend
                            if next_legend
                            else generate_legend(
                                xval if callable(xf) else xf,
                                yval if callable(yf) else yf,
                                lg_prefix,
                            )
                        )

                labels[0].append(get_label(x_plots, units))
                labels[1].append(get_label(y_plots, units))

        ax.set_xlabel(
            args.xlabel if args.xlabel is not None else combine_labels(labels[0])
        )
        ax.set_ylabel(
            args.ylabel if args.ylabel is not None else combine_labels(labels[1])
        )

        if not args.no_legend:
            ax.legend(
                bbox_to_anchor=(0.0, 1.1, 1.0, 0.1),
                loc="lower left",
                ncol=1,
                mode="expand",
                borderaxespad=0.0,
            )
        with output_to(args.output) as of:
            pickle.dump((fig, ax), of)
            of.flush()


if __name__ == "__main__":
    main()
