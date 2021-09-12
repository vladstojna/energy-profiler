#!/usr/bin/env python3

import json
import sys
import argparse
import copy
from typing import Any, Dict, List, Optional, Tuple, Union


def main():
    FormatType = Dict[str, List[str]]
    DataIndex = Dict[str, int]
    ReadingsIndices = Dict[str, List[int]]
    MergedIndices = Dict[str, ReadingsIndices]
    Sample = List[Union[int, float]]
    ReadingsType = Dict[str, Union[int, List[Sample]]]
    SampleTimes = Union[List[int], List[float]]
    Relative = Union[None, bool, Union[int, float]]

    choices = ("monotonic", "nonmonotonic", "m", "nm")

    def log(*args: Any) -> None:
        print("{}:".format(sys.argv[0]), *args, file=sys.stderr)

    def read_from(path: Optional[str]):
        return sys.stdin if not path else open(path, "r")

    def output_to(path: Optional[str]):
        return sys.stdout if not path else open(path, "w")

    def add_arguments(parser: argparse.ArgumentParser) -> argparse.ArgumentParser:
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
                        err.args[0]
                        if len(err.args)
                        else "could not convert value to float"
                    )

        def add_relative(parser: argparse.ArgumentParser, argname: str) -> None:
            parser.add_argument(
                argname,
                action="store",
                help="""convert to relative values starting at 0
                    if no argument provided or starting at first = first - QUANTITY""",
                nargs="?",
                type=positive_int_or_float,
                required=False,
                default=None,
                const=True,
                metavar="QUANTITY",
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

    def merge_data_indices(*dis: Tuple[str, DataIndex]) -> MergedIndices:
        retval: MergedIndices = {}
        for key, di in dis:
            for tgt, idx in di.items():
                if tgt in retval:
                    if key in retval[tgt]:
                        retval[tgt][key].append(idx)
                    else:
                        retval[tgt][key] = [idx]
                else:
                    retval[tgt] = {}
                    if key not in retval[tgt]:
                        retval[tgt][key] = [idx]
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

    def relative_sample_times(sample_times: SampleTimes, rel: Relative) -> None:
        first = sample_times[0] if rel is True else rel
        if first > 0:
            for ix in range(0, len(sample_times)):
                sample_times[ix] -= first

    def relative_readings(
        readings: List[ReadingsType],
        indices: ReadingsIndices,
        rel_energy: Relative,
        rel_time: Relative,
    ) -> None:
        def to_relative(
            samples: List[Sample],
            indices: ReadingsIndices,
            renergy: Relative,
            rtime: Relative,
        ) -> None:
            def get_first(
                sample: Sample,
                indices: ReadingsIndices,
                renergy: Relative,
                rtime: Relative,
            ) -> List[Sample]:
                retval = copy.deepcopy(sample)
                if renergy is True and rtime is True:
                    return retval
                eixs = indices.get("energy")
                tixs = indices.get("time")
                if renergy is not True and eixs:
                    for i in eixs:
                        retval[i] = renergy
                if rtime is not True and tixs:
                    for i in tixs:
                        retval[i] = rtime
                return retval

            def all_zero_or_negative(sample: Sample, indices: ReadingsIndices) -> bool:
                for ix in [i for v in indices.values() for i in v]:
                    if sample[ix] > 0:
                        return False
                return True

            first = get_first(samples[0], indices, renergy, rtime)
            if not all_zero_or_negative(first, indices):
                for s in range(0, len(samples)):
                    for ix in [i for v in indices.values() for i in v]:
                        samples[s][ix] -= first[ix]

        for skt_readings in readings:
            for samples in (
                v for _, v in skt_readings.items() if isinstance(v, list) and v
            ):
                to_relative(samples, indices, rel_energy, rel_time)

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
            merge_data_indices(("time", time_indices), ("energy", {}))
            if to_nonmonotonic(args.energy) or not args.relative_energy
            else merge_data_indices(("time", time_indices), ("energy", energy_indices))
        )

        if any_conversion(args.energy):
            # remove the 'idle' object because it is only used
            # properly when energy is increasing monotonically
            json_in["idle"] = []
        else:
            for i in json_in["idle"]:
                if args.relative_time:
                    relative_sample_times(i["sample_times"], args.relative_time)
                for tgt, idx in ((k, v) for k, v in merged_ixs.items() if i.get(k)):
                    relative_readings(
                        i[tgt], idx, args.relative_energy, args.relative_time
                    )

        for g in json_in["groups"]:
            for s in g["sections"]:
                for e in s["executions"]:
                    if args.relative_time:
                        relative_sample_times(e["sample_times"], args.relative_time)
                    for rds, idx in (
                        (e[k], v) for k, v in energy_indices.items() if e.get(k)
                    ):
                        convert_readings(rds, args.energy, idx)
                    for rds, idxs in (
                        (e[k], v) for k, v in merged_ixs.items() if e.get(k)
                    ):
                        relative_readings(
                            rds, idxs, args.relative_energy, args.relative_time
                        )

        write_output(json_in, args.output)


if __name__ == "__main__":
    main()
