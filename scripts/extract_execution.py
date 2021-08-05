#!/usr/bin/env python3

import json
import sys
import argparse


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
        "--exec",
        action="store",
        help="execution index",
        required=False,
        type=int,
        default=0,
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
        "--device",
        action="store",
        help="device/socket index",
        required=True,
        type=int,
        default=0,
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


def get_execution(execs, idx):
    try:
        return execs[idx]
    except IndexError as err:
        raise ValueError("Execution with index '{}' does not exist".format(idx))


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
        execution = get_execution(section["executions"], args.exec)
        dev_key = "socket" if args.target == "cpu" else "device"
        sensors = find(
            execution,
            args.device,
            args.target,
            dev_key,
        )
        if args.location and not sensors.get(args.location):
            raise ValueError("Location {} does not exist".format(args.location))

        sample_times = execution["sample_times"]
        sample_format = json_in["format"][args.target]

        format_comment = []
        format_comment.append("sample")
        format_comment.append("sample_time")

        value_list = [[ix, sample_times[ix]] for ix in range(len(sample_times))]
        for loc, samples in sorted(
            {
                k: v
                for k, v in sensors.items()
                if k != dev_key
                and v
                and (k == args.location if args.location else True)
            }.items()
        ):
            if len(value_list) != len(samples):
                raise AssertionError("'sample_times' length != '{}' length".format(loc))
            for dt in sample_format:
                format_comment.append(dt + "_" + str(loc))

            for lst, smp in zip(value_list, samples):
                if len(smp) != len(sample_format):
                    raise AssertionError("format length != sample length")
                for value, dt in zip(smp, sample_format):
                    lst.append(value)

        with output_to(args.output) as o:
            print(f"#{args.target}", file=o)
            print(f"#{dev_key} {args.device}", file=o)
            print(f"#{args.group}", file=o)
            print(f"#{args.section}", file=o)
            print(f"#execution {args.exec}", file=o)
            print(f"{','.join(format_comment)}", file=o)
            for values in value_list:
                print(",".join(str(e) for e in values), file=o)


if __name__ == "__main__":
    main()
