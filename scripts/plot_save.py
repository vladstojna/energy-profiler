#!/usr/bin/env python3

import sys
import argparse
import pickle
import matplotlib
import matplotlib.figure
import matplotlib.axes
import matplotlib.pyplot as plt
from typing import Any, Callable, Tuple


def read_from(path: str) -> Any:
    return sys.stdin if not path else open(path, "rb")


def output_to(path: str) -> Any:
    return sys.stdout if not path else open(path, "wb")


def add_arguments(parser: argparse.ArgumentParser) -> argparse.ArgumentParser:
    def _positive_number(s: str, convert: Callable) -> Any:
        try:
            val = convert(s)
            if val <= 0:
                raise argparse.ArgumentTypeError("value must be positive")
            return val
        except ValueError as err:
            raise argparse.ArgumentTypeError(
                err.args[0] if len(err.args) else "could not convert value"
            )

    def positive_integer(s: str) -> int:
        return _positive_number(s, convert=int)

    def positive_decimal(s: str) -> float:
        return _positive_number(s, convert=float)

    parser.add_argument(
        "source_file",
        action="store",
        help="plot file to save (default: stdin)",
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
        "-b",
        "--backend",
        action="store",
        type=str,
        help="backend to use when generating plot (default: agg)",
        required=False,
        choices=["agg", "pdf", "svg"],
        default="agg",
    )
    parser.add_argument(
        "--dpi",
        action="store",
        help="image DPI (only has an effect when backend is agg)",
        required=False,
        type=positive_integer,
        default=0,
    )
    parser.add_argument(
        "-H",
        "--height",
        help="figure height in cm",
        required=False,
        type=positive_decimal,
        default=0,
    )
    parser.add_argument(
        "-W",
        "--width",
        help="figure width in cm",
        required=False,
        type=positive_decimal,
        default=0,
    )
    return parser


def cm2inch(width: float, height: float) -> Tuple[float, float]:
    inch = 2.54
    return width / inch, height / inch


def deserialize(f: Any) -> Tuple[matplotlib.figure.Figure, matplotlib.axes.Axes]:
    return pickle.load(f)


def main():
    parser = argparse.ArgumentParser(description="Save serialized plot as an image")
    args = add_arguments(parser).parse_args()
    if args.height and not args.width:
        parser.error("-h/--height requires -w/--width")
    if args.width and not args.height:
        parser.error("-w/--width requires -h/--height")
    matplotlib.use(args.backend)
    with read_from(args.source_file) as f:
        with plt.ioff():
            fig, _ = deserialize(f)
            fig.tight_layout()
            if args.backend == "agg" and args.dpi:
                fig.set_dpi(args.dpi)
            if args.width and args.height:
                fig.set_size_inches(cm2inch(args.width, args.height))
            with output_to(args.output) as of:
                fig.savefig(of)


if __name__ == "__main__":
    main()
