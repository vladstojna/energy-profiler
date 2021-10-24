#!/usr/bin/env python3

import json
import csv
import argparse

from extract import common as extr


def main():
    parser = argparse.ArgumentParser(
        description="Extract execution values from compacted JSON in CSV format"
    )
    extr.add_arguments(parser)
    args = parser.parse_args()
    with extr.read_from(args.source_file) as f:
        json_in = json.load(f)
        execs = extr.initial_exec_lookup(args, json_in)

        dev_key = extr.target_choices[args.target]
        unit_row = {k: v for k, v in json_in["units"].items()}
        format_row = ["count", "execution", "time"]
        prefix = "energy"
        result = []
        for count, (idx, execution) in enumerate(zip(args.execs, execs)):
            time = execution["time"]
            if execution.get(args.target) is None:
                raise ValueError("Target '{}' does not exist".format(args.target))

            args.devs = extr.find_devices(execution[args.target], args.devs, dev_key)

            value_list = [count, idx, time]

            for dev in args.devs:
                sensors = extr.find(execution, dev, args.target, dev_key)
                if args.location and sensors.get(args.location) is None:
                    raise ValueError(
                        "Location '{}' does not exist".format(args.location)
                    )
                for loc, values in sorted(
                    {
                        k: v
                        for k, v in sensors.items()
                        if k != dev_key
                        and v
                        and (k == args.location if args.location else True)
                    }.items()
                ):
                    for value_type, value in values.items():
                        entry = (
                            "{}_{}_{}_{}{!s}".format(
                                value_type, prefix, loc, dev_key, dev
                            )
                            if len(args.devs) > 1
                            else "{}_{}_{}".format(value_type, prefix, loc)
                        )
                        if entry not in format_row:
                            format_row.append(entry)
                        value_list.append(value)
            result.append(value_list)

        with extr.output_to(args.output) as o:
            wrt = csv.writer(o)
            wrt.writerow(["#group", args.group])
            wrt.writerow(["#section", args.section])
            wrt.writerow(
                ["#devices"]
                + ["{}_{}={!s}".format(args.target, dev_key, d) for d in args.devs]
            )
            wrt.writerow(
                ["#units"] + ["{}={}".format(k, v) for k, v in unit_row.items()]
            )
            wrt.writerow(format_row)
            wrt.writerows(result)


if __name__ == "__main__":
    main()
