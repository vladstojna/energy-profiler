#!/usr/bin/env python3

import json
import sys
import argparse
import units.units as units

from typing import Any, Iterable, List, Optional, Dict, Tuple, Union


def main():
    choices = ("energy", "power")
    targets = {"cpu": "socket", "gpu": "device"}

    class data_idx:
        def __init__(self, tp, idx):
            self.type = tp
            self.idx = idx

        def __bool__(self):
            return self.idx != -1

        def __str__(self):
            return "({},{})".format(self.type, self.idx)

    FormatType = Dict[str, List[str]]
    UnitDictType = Dict[str, units.Fraction]

    ConversionItem = Tuple[data_idx, data_idx]
    ConversionMap = Dict[str, ConversionItem]

    SampleTimes = List[int]
    Sample = List[Union[int, float]]
    ReadingsType = Dict[str, Union[int, List[Sample]]]

    def log(*args: Any) -> None:
        print("{}:".format(sys.argv[0]), *args, file=sys.stderr)

    def read_from(path: Optional[str]):
        return sys.stdin if not path else open(path, "r")

    def output_to(path: Optional[str]):
        return sys.stdout if not path else open(path, "w")

    def add_args(parser: argparse.ArgumentParser) -> argparse.ArgumentParser:
        parser.add_argument(
            "source_file",
            action="store",
            help="file to compact (default: stdin)",
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
            help="what type of data to convert to",
            required=True,
            type=str,
            default=None,
            choices=choices,
        )
        return parser

    def deserialize_units(raw: Dict[str, str]) -> UnitDictType:
        def _str_to_frac(sym: str, fracs: Dict[units.Fraction, str]) -> units.Fraction:
            for k, v in fracs.items():
                if v == sym:
                    return k
            raise ValueError("Unsupported units found")

        retval = {}
        type_map = {"energy": units.Energy, "power": units.Power, "time": units.Time}
        for utype, usym in raw.items():
            retval[utype] = _str_to_frac(usym, type_map[utype].units)
        return retval

    def find_data_idx(fmt: List[str], to_find: Iterable[str]) -> data_idx:
        def format_index_of(fmt: List[str], data_type: str) -> int:
            try:
                return fmt.index(data_type)
            except ValueError:
                return -1

        for d in to_find:
            idx = format_index_of(fmt, d)
            if idx != -1:
                return data_idx(d, idx)
        return data_idx(None, -1)

    def energy2power(
        times: SampleTimes,
        readings: Iterable[ReadingsType],
        value_idx: int,
        dev_key: str,
        units_in: UnitDictType,
    ) -> None:
        def generator(
            times: SampleTimes, samples: List[Sample], idx: int, u: UnitDictType
        ):
            return (
                (
                    units.div(
                        units.Energy(s[idx] - samples[ix - 1][idx], u["energy"]),
                        units.Time(t - times[ix - 1], u["time"]),
                    ).convert(u["power"]),
                    ix,
                )
                for s, t, ix in zip(samples[1:], times[1:], range(1, len(samples)))
            )

        for sensors in readings:
            for samples in (v for k, v in sensors.items() if k != dev_key and v):
                assert len(samples) == len(times)
                if len(samples) != len(times):
                    raise AssertionError("len(samples) != len(sample_times)")
                val = units.Power(0.0)
                for p, ix in generator(times, samples, value_idx, units_in):
                    samples[ix - 1][value_idx] = val.value
                    val = p
                samples[-1][value_idx] = val.value
        for ix, t in enumerate(times[1:]):
            times[ix + 1] = (t + times[ix]) // 2

    def power2energy(
        times: SampleTimes,
        readings: Iterable[ReadingsType],
        value_idx: int,
        dev_key: str,
        units_in: UnitDictType,
    ) -> None:
        def generator(
            times: SampleTimes, samples: List[Sample], idx: int, u: UnitDictType
        ):
            return (
                (
                    units.mul(
                        units.Power((s[idx] + samples[ix - 1][idx]) / 2, u["power"]),
                        units.Time(t - times[ix - 1], u["time"]),
                    ).convert(u["energy"]),
                    ix,
                )
                for s, t, ix in zip(samples[1:], times[1:], range(1, len(samples)))
            )

        for sensors in readings:
            for samples in (v for k, v in sensors.items() if k != dev_key and v):
                assert len(samples) == len(times)
                if len(samples) != len(times):
                    raise AssertionError("len(samples) != len(sample_times)")
                val = units.Energy(0.0)
                for e, ix in generator(times, samples, value_idx, units_in):
                    samples[ix - 1][value_idx] = val.value
                    val += e
                samples[-1][value_idx] = val.value

    def power2energy_with_times(
        times: SampleTimes,
        readings: Iterable[ReadingsType],
        time_idx: int,
        value_idx: int,
        dev_key: str,
        units_in: UnitDictType,
    ) -> None:
        def generator(samples: List[Sample], tidx: int, vidx: int, u: UnitDictType):
            return (
                (
                    units.mul(
                        units.Power((s[vidx] + samples[ix - 1][vidx]) / 2, u["power"]),
                        units.Time(s[tidx] - samples[ix - 1][tidx], u["time"]),
                    ).convert(u["energy"]),
                    ix,
                )
                for ix, s in enumerate(samples[1:], start=1)
            )

        for sensors in readings:
            for samples in (v for k, v in sensors.items() if k != dev_key and v):
                assert len(samples) == len(times)
                if len(samples) != len(times):
                    raise AssertionError("len(samples) != len(sample_times)")
                val = units.Energy(0.0)
                for e, ix in generator(samples, time_idx, value_idx, units_in):
                    samples[ix - 1][value_idx] = val.value
                    val += e
                samples[-1][value_idx] = val.value

    def convert_execution(
        times: SampleTimes,
        readings: Iterable[ReadingsType],
        dev_key: str,
        stored: data_idx,
        sensor_times: data_idx,
        units_in: UnitDictType,
        convert_to: str,
    ) -> None:
        if stored.type == "energy":
            assert convert_to == "power"
            if sensor_times:
                msg = f"{stored.type}->{convert_to}: using sample_times instead of sensor_time"
                log(msg)
            energy2power(times, readings, stored.idx, dev_key, units_in)
        elif stored.type == "power":
            assert convert_to == "energy"
            if sensor_times:
                power2energy_with_times(
                    times, readings, sensor_times.idx, stored.idx, dev_key, units_in
                )
            else:
                power2energy(times, readings, stored.idx, dev_key, units_in)
        else:
            raise ValueError("cannot convert: not power or energy")

    parser = argparse.ArgumentParser(
        description="Convert output data to/from energy/power"
    )
    args = add_args(parser).parse_args()
    with read_from(args.source_file) as f:
        json_in = json.load(f)
        units_in = deserialize_units(json_in["units"])
        fmt: FormatType = json_in["format"]
        conversions: ConversionMap = {}
        for tgt in targets:
            di = find_data_idx(fmt[tgt], choices)
            if not di:
                s_choices = " or ".join(choices)
                s_fmt = ",".join(fmt[tgt]) if fmt[tgt] else "empty"
                log("{}: No {} data found in {} format".format(tgt, s_choices, s_fmt))
            if di and di.type != args.to:
                st = find_data_idx(fmt[tgt], ("sensor_time",))
                conversions[tgt] = di, st
                for ix in range(len(fmt[tgt])):
                    if fmt[tgt][ix] == di.type:
                        fmt[tgt][ix] = args.to
                log(
                    "{}:{}->{} (sensor_time={})".format(tgt, di.type, args.to, bool(st))
                )
        if not conversions:
            log("No data found to convert to {}".format(args.to))
            with output_to(args.output) as of:
                json.dump(json_in, of)
                return
        for i in json_in["idle"]:
            for tgt, dev_key in ((k, v) for k, v in targets.items() if i.get(k)):
                di, st = conversions[tgt]
                convert_execution(
                    i["sample_times"], i[tgt], dev_key, di, st, units_in, args.to
                )
        for g in json_in["groups"]:
            for s in g["sections"]:
                for e in s["executions"]:
                    for tgt, dev_key in (
                        (k, v) for k, v in targets.items() if e.get(k)
                    ):
                        di, st = conversions[tgt]
                        convert_execution(
                            e["sample_times"],
                            e[tgt],
                            dev_key,
                            di,
                            st,
                            units_in,
                            args.to,
                        )
        with output_to(args.output) as of:
            json.dump(json_in, of)


if __name__ == "__main__":
    main()
