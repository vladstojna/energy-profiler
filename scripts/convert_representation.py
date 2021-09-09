#!/usr/bin/env python3

import json
import sys
import argparse
import copy
from typing import Any, Dict, List, Optional, Union


def main():
    FormatType = Dict[str, List[str]]
    DataIndex = Dict[str, int]
    DataIndices = Dict[str, List[int]]

    Sample = List[Union[int, float]]
    ReadingsType = Dict[str, Union[int, List[Sample]]]
    SampleTimes = Union[List[int], List[float]]

    choices = ("monotonic", "nonmonotonic", "m", "nm")

    def log(*args: Any) -> None:
        print("{}:".format(sys.argv[0]), *args, file=sys.stderr)

    def read_from(path: Optional[str]):
        return sys.stdin if not path else open(path, "r")

    def output_to(path: Optional[str]):
        return sys.stdout if not path else open(path, "w")

    def add_arguments(parser: argparse.ArgumentParser) -> argparse.ArgumentParser:
        def add_relative(parser: argparse.ArgumentParser, argname: str) -> None:
            parser.add_argument(
                argname,
                action="store_true",
                help="convert to relative values starting at 0",
                required=False,
                default=False,
            )

        parser.add_argument(
            "source_file",
            action="store",
            help="file to convert (default: stdin)",
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
            "-e",
            "--energy",
            action="store",
            help="what type of representation to convert energy to (default: no conversion)",
            required=False,
            type=str,
            default=None,
            choices=choices,
        )
        add_relative(parser, "--relative-time")
        add_relative(parser, "--relative-energy")
        return parser

    def to_monotonic(arg: str) -> bool:
        return arg and (arg == choices[0] or arg == choices[2])

    def to_nonmonotonic(arg: str) -> bool:
        return arg and (arg == choices[1] or arg == choices[3])

    def any_conversion(arg: str) -> bool:
        return to_monotonic(arg) or to_nonmonotonic(arg)

    def get_indices(fmt: FormatType, data_type: str) -> DataIndex:
        def format_index_of(fmt: List[str], data_type: str) -> int:
            try:
                return fmt.index(data_type)
            except ValueError:
                return -1

        retval: DataIndex = {}
        for k, v in fmt.items():
            idx = format_index_of(v, data_type)
            if idx != -1:
                retval[k] = idx
        return retval

    def get_time_indices(fmt: FormatType) -> DataIndex:
        return get_indices(fmt, "sensor_time")

    def get_energy_indices(fmt: FormatType) -> DataIndex:
        return get_indices(fmt, "energy")

    def merge_data_indices(*dis: DataIndex) -> DataIndices:
        retval: DataIndices = {}
        for di in dis:
            for dt, idx in di.items():
                if dt in retval:
                    retval[dt].append(idx)
                else:
                    retval[dt] = [idx]
        return retval

    def convert_readings(
        readings: List[ReadingsType], conv_type: str, energy_idx: int
    ) -> None:
        def convert_to_monotonic(samples: List[Sample], idx: int) -> None:
            to_add = samples[0][idx]
            for i in range(1, len(samples)):
                samples[i][idx] += to_add
                to_add = samples[i][idx]

        def convert_to_nonmonotonic(samples: List[Sample], idx: int) -> None:
            to_sub = samples[0][idx]
            samples[0][idx] -= to_sub
            for i in range(1, len(samples)):
                tmp = samples[i][idx]
                samples[i][idx] -= to_sub
                to_sub = tmp

        if not any_conversion(conv_type):
            return

        conversion_func = None
        if to_monotonic(conv_type):
            conversion_func = convert_to_monotonic
        elif to_nonmonotonic(conv_type):
            conversion_func = convert_to_nonmonotonic
        else:
            raise AssertionError("Invalid conversion type")

        for skt_readings in readings:
            for samples in (
                v for _, v in skt_readings.items() if isinstance(v, list) and v
            ):
                conversion_func(samples, energy_idx)

    def relative_sample_times(sample_times: SampleTimes) -> None:
        first = sample_times[0]
        if first > 0:
            for ix in range(0, len(sample_times)):
                sample_times[ix] -= first

    def relative_readings(readings: List[ReadingsType], indices: List[int]) -> None:
        def to_relative(samples: List[Sample], indices: List[int]) -> None:
            def all_zero(sample: Sample, indices: List[int]) -> bool:
                for ix in indices:
                    if sample[ix] > 0:
                        return False
                return True

            first = copy.deepcopy(samples[0])
            if not all_zero(first, indices):
                for s in range(0, len(samples)):
                    for ix in indices:
                        samples[s][ix] -= first[ix]

        for skt_readings in readings:
            for samples in (
                v for _, v in skt_readings.items() if isinstance(v, list) and v
            ):
                to_relative(samples, indices)

    def convert_idle(idle) -> None:
        for i in idle:
            if args.relative_time:
                relative_sample_times(i["sample_times"])
            for tgt, idx in ((k, v) for k, v in merged_ixs.items() if i.get(k)):
                relative_readings(i[tgt], idx)

    def write_output(json_in, output_path: str) -> None:
        with output_to(output_path) as of:
            json.dump(json_in, of)

    parser = argparse.ArgumentParser(
        description="""Convert energy from monotonic to non-monotonic and vice-versa;
            convert time and/or energy to relative values"""
    )
    args = add_arguments(parser).parse_args()
    with read_from(args.source_file) as f:
        json_in = json.load(f)

        if (
            not any_conversion(args.energy)
            and not args.relative_time
            and not args.relative_energy
        ):
            log("No conversions done")
            write_output(json_in, args.output)
            return

        fmt: FormatType = json_in["format"]

        energy_indices = (
            get_energy_indices(fmt)
            if any_conversion(args.energy) or args.relative_energy
            else {}
        )

        if not energy_indices and (any_conversion(args.energy) or args.relative_energy):
            log("Input file format has no energy data; no energy conversions done")
            if not args.relative_time:
                log("No conversions done")
                write_output(json_in, args.output)
                return

        time_indices = get_time_indices(fmt) if args.relative_time else {}
        merged_ixs = (
            merge_data_indices(time_indices)
            if to_nonmonotonic(args.energy) or not args.relative_energy
            else merge_data_indices(energy_indices, time_indices)
        )

        if any_conversion(args.energy):
            # remove the 'idle' object because it is only used
            # properly when energy is increasing monotonically
            json_in["idle"] = []
        elif args.relative_time or args.relative_energy:
            convert_idle(json_in["idle"])

        for g in json_in["groups"]:
            for s in g["sections"]:
                for e in s["executions"]:
                    if args.relative_time:
                        relative_sample_times(e["sample_times"])
                    for rds, idx in (
                        (e[k], v) for k, v in energy_indices.items() if e.get(k)
                    ):
                        convert_readings(rds, args.energy, idx)
                    for rds, idxs in (
                        (e[k], v) for k, v in merged_ixs.items() if e.get(k)
                    ):
                        relative_readings(rds, idxs)

        write_output(json_in, args.output)


if __name__ == "__main__":
    main()
