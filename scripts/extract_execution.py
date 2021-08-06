#!/usr/bin/env python3

import json
import sys
import argparse
from typing import Iterable, Union


class intlist_or_all(argparse.Action):
    def __init__(
        self,
        option_strings,
        dest=None,
        nargs="+",
        **kwargs,
    ) -> None:
        super().__init__(option_strings, dest=dest, nargs=nargs, **kwargs)

    def __call__(
        self,
        parser,
        namespace,
        values,
        option_string,
    ) -> None:
        if len(values) == 1 and next(iter(values)).lower() == "all":
            setattr(namespace, self.dest, "all")
        else:
            try:
                setattr(namespace, self.dest, sorted([int(e) for e in values]))
            except ValueError:
                raise argparse.ArgumentError(
                    self, "values must be base 10 integers or 'all'"
                )


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
        "-g",
        "--group",
        action="store",
        help="group label",
        required=False,
        type=str,
        default=None,
    )
    parser.add_argument(
        "-s",
        "--section",
        action="store",
        help="section label",
        required=False,
        type=str,
        default=None,
    )
    parser.add_argument(
        "-e",
        "--execs",
        "--executions",
        action=intlist_or_all,
        help="execution index(es) or 'all'",
        required=False,
        nargs="+",
        default=[],
    )
    parser.add_argument(
        "-t",
        "--target",
        action="store",
        help="hardware device target",
        required=True,
        type=str,
        choices=["cpu", "gpu"],
    )
    parser.add_argument(
        "-d",
        "--devs",
        "--devices",
        action=intlist_or_all,
        help="device/socket index(es) or 'all'",
        required=True,
        nargs="+",
        default=[],
    )
    parser.add_argument(
        "-l",
        "--location",
        action="store",
        help="sensor location",
        required=False,
        type=str,
        default=None,
    )
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


def find(json_in, arg, attr, attr_compare):
    if not json_in.get(attr):
        raise AssertionError("attribute '{}' not found".format(attr))
    if not json_in[attr]:
        raise AssertionError("'{}' empty".format(attr))
    if not arg:
        return json_in[attr][0]
    else:
        retval = next((v for v in json_in[attr] if v[attr_compare] == arg), None)
        if not retval:
            raise ValueError(
                "{} '{}' not found in '{}'".format(attr_compare, arg, attr)
            )
        return retval


def get_executions(execs, idxs):
    if idxs == "all":
        return execs
    try:
        retval = []
        for ix in idxs:
            retval.append(execs[ix])
        return retval
    except IndexError as err:
        raise ValueError("Execution with index '{}' does not exist".format(ix))


def find_devices(
    readings: Iterable, to_find: Union[Iterable, str], dev_key: str
) -> list:
    retval = sorted(
        [x[dev_key] for x in readings if to_find == "all" or x[dev_key] in to_find]
    )
    if to_find != "all":
        for d in to_find:
            if d not in retval:
                raise ValueError("{} {} does not exist in readings".format(dev_key, d))
    return retval


def main():
    parser = argparse.ArgumentParser(
        description="Extract execution values in CSV format"
    )
    add_arguments(parser)
    args = parser.parse_args()
    with read_from(args.source_file) as f:
        json_in = json.load(f)
        group = find(json_in, args.group, "groups", "label")
        section = find(group, args.section, "sections", "label")

        if not section["executions"]:
            raise ValueError("Execution is empty")
        if not args.execs:
            args.execs.append(0)
        execs = get_executions(section["executions"], args.execs)
        if args.execs == "all":
            args.execs = [x for x in range(len(execs))]
        if len(execs) != len(args.execs):
            raise AssertionError("exec count != indices requested")

        sample_format = json_in["format"][args.target]
        unit_row = {k: v for k, v in json_in["units"].items()}
        exec_row = []
        format_row = ["sample", "sample_time"]
        result = []
        accum_sample = 0
        dev_key = "socket" if args.target == "cpu" else "device"
        for execution, idx in zip(execs, args.execs):
            sample_times = execution["sample_times"]
            args.devs = find_devices(execution[args.target], args.devs, dev_key)
            value_list = [
                [accum_sample + ix, sample_times[ix]] for ix in range(len(sample_times))
            ]
            exec_row.append(tuple((idx, accum_sample, len(sample_times))))
            accum_sample += len(sample_times)
            for dev in args.devs:
                sensors = find(execution, dev, args.target, dev_key)
                if args.location and sensors.get(args.location) == None:
                    raise ValueError(
                        "Location '{}' does not exist".format(args.location)
                    )
                for loc, samples in sorted(
                    {
                        k: v
                        for k, v in sensors.items()
                        if k != dev_key
                        and v
                        and (k == args.location if args.location else True)
                    }.items()
                ):
                    if len(sample_times) != len(samples):
                        raise AssertionError(
                            "'sample_times' length != '{}' length".format(loc)
                        )
                    for dt in sample_format:
                        entry = "{}_{}_{}{!s}".format(dt, loc, dev_key, dev)
                        if entry not in format_row:
                            format_row.append(entry)
                    for lst, smp in zip(value_list, samples):
                        if len(smp) != len(sample_format):
                            raise AssertionError("format length != sample length")
                        for value in smp:
                            lst.append(value)
                result.append(value_list)

        with output_to(args.output) as o:
            print(f"#{args.group}", file=o)
            print(f"#{args.section}", file=o)

            print(
                "#units,{}".format(
                    ",".join("{}={}".format(k, v) for k, v in unit_row.items())
                )
            )
            print(
                "#executions,{}".format(
                    ",".join(
                        "i={!s}|start={!s}|size={!s}".format(i, s, sz)
                        for i, s, sz in exec_row
                    )
                ),
                file=o,
            )
            print(",".join(format_row), file=o)
            for vl in result:
                for values in vl:
                    print(",".join(str(e) for e in values), file=o)


if __name__ == "__main__":
    main()
