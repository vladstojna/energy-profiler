#!/usr/bin/env python3

import json
import sys
import argparse
from typing import Dict, List, Optional, Union


def main():
    FormatType = Dict[str, List[str]]
    EnergyIdxType = Dict[str, int]

    Sample = List[Union[int, float]]
    ReadingsType = Dict[str, Union[int, List[Sample]]]

    choices = ("monotonic", "nonmonotonic")

    def read_from(path: Optional[str]):
        return sys.stdin if not path else open(path, "r")

    def output_to(path: Optional[str]):
        return sys.stdout if not path else open(path, "w")

    def add_arguments(parser: argparse.ArgumentParser) -> argparse.ArgumentParser:
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
            "-t",
            "--to",
            action="store",
            help="what type of representation to convert to",
            required=False,
            type=str,
            default=choices[1],
            choices=choices,
        )
        return parser

    def get_energy_indices(fmt: FormatType) -> EnergyIdxType:
        def format_index_of(fmt: List[str], data_type: str) -> int:
            try:
                return fmt.index(data_type)
            except ValueError:
                return -1

        retval: EnergyIdxType = {}
        for k, v in fmt.items():
            idx = format_index_of(v, "energy")
            if idx != -1:
                retval[k] = idx
        return retval

    def convert_readings(
        readings: List[ReadingsType], monotonic: bool, energy_idx: int
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

        conversion_func = convert_to_monotonic if monotonic else convert_to_nonmonotonic
        for skt_readings in readings:
            for loc, samples in (
                (k, v) for k, v in skt_readings.items() if isinstance(v, list) and v
            ):
                conversion_func(samples, energy_idx)

    parser = argparse.ArgumentParser(
        description="Convert energy readings from monotonic to non-monotonic and vice-versa"
    )
    args = add_arguments(parser).parse_args()

    monotonic = True if args.to == choices[0] else False
    with read_from(args.source_file) as f:
        json_in = json.load(f)
        fmt: FormatType = json_in["format"]
        energy_indices: EnergyIdxType = get_energy_indices(fmt)
        if not energy_indices:
            raise ValueError("Input file format has no energy data")

        # remove the 'idle' object because it is only used properly when energy is
        # increasing monotonically
        json_in["idle"] = []
        for g in json_in["groups"]:
            for s in g["sections"]:
                for e in s["executions"]:
                    for readings, idx in (
                        (e[k], v) for k, v in energy_indices.items() if e.get(k)
                    ):
                        convert_readings(readings, monotonic, idx)

        with output_to(args.output) as of:
            json.dump(json_in, of)


if __name__ == "__main__":
    main()
