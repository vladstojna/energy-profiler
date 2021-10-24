#!/usr/bin/env python3

import csv
import argparse
import sys
from typing import Any, Callable, Iterable, Optional, Tuple


def log(*args: Any) -> None:
    print("{}:".format(sys.argv[0]), *args, file=sys.stderr)


def read_from(path: Optional[str]):
    return sys.stdin if not path else open(path, "r")


def output_to(path: Optional[str]):
    return sys.stdout if not path else open(path, "w")


def add_arguments(parser: argparse.ArgumentParser) -> argparse.ArgumentParser:
    parser.add_argument(
        "source_file",
        action="store",
        help="file to filter (default: stdin)",
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
        "--zero",
        action="store_true",
        help="filter when any rows[n].val == 0 (default: any rows[n].val == rows[n-1].val)",
        required=False,
        default=False,
    )
    parser.add_argument(
        "--fields",
        action="store",
        help="which fields to consider when filtering a row (default: all)",
        required=False,
        nargs="+",
        default=None,
    )
    return parser


def read_file(f) -> Tuple[csv.reader, csv.DictReader]:
    comments = []
    rest = []
    for row in f:
        if row.lstrip().rstrip().startswith("#"):
            comments.append(row)
        else:
            rest.append(row)
    return csv.reader(comments), csv.DictReader(rest)


def unique_row(
    prev_row: Iterable, curr_row: Iterable, fields: Iterable[str], pred: Callable
) -> bool:
    if len(prev_row) != len(curr_row):
        raise AssertionError("Malformed file, rows have a different number of columns")
    for v1, v2 in ((prev_row[f], curr_row[f]) for f in fields):
        if pred(v1, v2):
            return False
    return True


def assert_fields(fields: Iterable[str], fieldnames: Iterable[str]):
    for f in fields:
        if f not in fieldnames:
            raise AssertionError("{} is not a valid fieldname".format(f))


def main():
    parser = argparse.ArgumentParser(
        description="""Filter duplicate readings from CSV file;
            one field which satisfies the condition
            is enough to make the row a candidate for removal"""
    )
    args = add_arguments(parser).parse_args()
    pred = (lambda _, y: not float(y)) if args.zero else (lambda x, y: x == y)
    with read_from(args.source_file) as f:
        comments, data = read_file(f)
        if args.fields:
            assert_fields(args.fields, data.fieldnames)
        else:
            args.fields = data.fieldnames
        line_num = 0
        with output_to(args.output) as of:
            csv.writer(of).writerows(comments)
            line_num += comments.line_num
            dwriter = csv.DictWriter(of, data.fieldnames)
            # fieldnames row
            dwriter.writeheader()
            # first data row
            prev_row = next(iter(data), None)
            if prev_row is not None:
                dwriter.writerow(prev_row)
                for row in data:
                    if unique_row(prev_row, row, args.fields, pred=pred):
                        dwriter.writerow(row)
                        prev_row = row
                    else:
                        log("Filtered out line", line_num + data.line_num)


if __name__ == "__main__":
    main()
