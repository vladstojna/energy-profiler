#!/usr/bin/env python3

import json
import csv
import argparse

from extract import common as extr


def main():
    parser = argparse.ArgumentParser(
        description="Extract execution values in CSV format"
    )
    extr.add_arguments(parser)
    args = parser.parse_args()
    with extr.read_from(args.source_file) as f:
        json_in = json.load(f)
        execs = extr.initial_exec_lookup(args, json_in)

        dev_key = extr.target_choices[args.target]
        sample_format = json_in["format"][args.target]
        unit_row = {k: v for k, v in json_in["units"].items()}
        exec_row = []
        format_row = ["sample", "sample_time"]
        result = []
        accum_sample = 0
        for execution, idx in zip(execs, args.execs):
            sample_times = execution["sample_times"]
            if execution.get(args.target) == None:
                raise ValueError("Target '{}' does not exist".format(args.target))
            args.devs = extr.find_devices(execution[args.target], args.devs, dev_key)
            value_list = [
                [accum_sample + ix, sample_times[ix]] for ix in range(len(sample_times))
            ]
            exec_row.append(tuple((idx, accum_sample, len(sample_times))))
            accum_sample += len(sample_times)
            for dev in args.devs:
                sensors = extr.find(execution, dev, args.target, dev_key)
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
                        entry = (
                            "{}_{}_{}{!s}".format(dt, loc, dev_key, dev)
                            if len(args.devs) > 1
                            else "{}_{}".format(dt, loc)
                        )
                        if entry not in format_row:
                            format_row.append(entry)
                    for lst, smp in zip(value_list, samples):
                        if len(smp) != len(sample_format):
                            raise AssertionError("format length != sample length")
                        for value in smp:
                            lst.append(value)
            result.extend(value_list)

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
            wrt.writerow(
                ["#executions"]
                + [
                    "i={!s}|start={!s}|size={!s}".format(i, s, sz)
                    for i, s, sz in exec_row
                ]
            )
            wrt.writerow(format_row)
            wrt.writerows(result)


if __name__ == "__main__":
    main()
