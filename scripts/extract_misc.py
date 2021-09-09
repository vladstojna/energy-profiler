#!/usr/bin/env python3

import json
import sys
import csv
import argparse


def read_from(path: str):
    return sys.stdin if not path else open(path, "r")


def get_devices(execution, target, attr):
    if not execution.get(target):
        return []
    return [d[attr] for d in execution[target]]


def join_value(lst):
    return "|".join(str(e) for e in lst) if lst else ""


def main():
    parser = argparse.ArgumentParser(description="Extract miscellaneous data")
    parser.add_argument(
        "source_file",
        action="store",
        type=str,
        default=None,
        nargs="?",
        help="file to extract from",
    )
    args = parser.parse_args()
    with read_from(args.source_file) as f:
        json_in = json.load(f)
        csvwrt = csv.writer(sys.stdout)
        formatstr = ["group", "section", "exec_count", "cpu_sockets", "gpu_devices"]
        csvwrt.writerow(formatstr)
        for g in json_in["groups"]:
            for s in g["sections"]:
                values = [g["label"], s["label"], len(s["executions"])]
                if s["executions"]:
                    values.append(
                        join_value(get_devices(s["executions"][0], "cpu", "socket"))
                    )
                    values.append(
                        join_value(get_devices(s["executions"][0], "gpu", "device"))
                    )
                else:
                    values.append("")
                    values.append("")
                if len(values) != len(formatstr):
                    raise AssertionError("Number of value entries do no match format")
                csvwrt.writerow(values)


if __name__ == "__main__":
    main()
