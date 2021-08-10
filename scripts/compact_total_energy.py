#!/usr/bin/env python3

import json
import sys
import copy
import argparse
from units import units
from typing import Dict, Iterable, List, Tuple, Union


FormatType = Dict[str, List[str]]
UnitDictType = Dict[str, units.Fraction]
SampleTimes = List[int]
Sample = List[Union[int, float]]
ReadingsType = List[Dict[str, Union[List[Sample], int]]]


class data_type_not_found(Exception):
    def __init__(self, message) -> None:
        self.message = message


class data_idx:
    def __init__(self, tp, idx):
        self.type = tp
        self.idx = idx

    def __bool__(self):
        return self.idx != -1

    def __str__(self):
        return "({},{})".format(self.type, self.idx)


def read_from(path):
    return sys.stdin if not path else open(path, "r")


def output_to(path):
    return sys.stdout if not path else open(path, "w")


def add_arguments(parser):
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
        "--no-delta",
        action="store_false",
        dest="delta",
        help="whether to not compute delta energy",
        required=False,
    )


def get_duration(sample_times: List[int], as_unit: units.Fraction = units.base):
    return units.Time(sample_times[-1] - sample_times[0], as_unit).as_float()


def format_index_of(fmt, data_type):
    try:
        return fmt.index(data_type)
    except ValueError:
        return -1


def find_data_idx(fmt, to_find):
    for d in to_find:
        idx = format_index_of(fmt, d)
        if idx != -1:
            return data_idx(d, idx)
    return data_idx(None, -1)


def get_total_from_accumulated_energy(
    samples: List[List[Union[int, float]]],
    idx: int,
    as_unit: units.Fraction = units.base,
):
    return units.Energy(float(samples[-1][idx] - samples[0][idx]), as_unit)


def _as_series(
    values: Iterable[Tuple[int, Union[int, float]]],
    time_units: units.Fraction,
    power_units: units.Fraction,
) -> Iterable[Tuple[units.Power, units.Time]]:
    return [(units.Power(p, power_units), units.Time(t, time_units)) for t, p in values]


def get_total_from_power_samples(
    timestamps: List[int],
    samples: List[List[Union[int, float]]],
    idx: int,
    units_in: Tuple[units.Fraction, units.Fraction],
    units_out: units.Fraction,
):
    if len(timestamps) != len(samples):
        raise AssertionError("Different lengths of samples and timestamps")
    return units.integrate_power_series(
        _as_series(
            ((t, float(lst[idx])) for t, lst in zip(timestamps, samples)),
            units_in[0],
            units_in[1],
        ),
        units_out,
    )


def get_total_from_timed_power_samples(
    samples: List[List[Union[int, float]]],
    idx_time: int,
    idx_power: int,
    units_in: Tuple[units.Fraction, units.Fraction],
    units_out: units.Fraction,
):
    return units.integrate_power_series(
        _as_series(
            ((lst[idx_time], float(lst[idx_power])) for lst in samples),
            units_in[0],
            units_in[1],
        ),
        units_out,
    )


def compute_idle(
    fmt: FormatType,
    dev_type: str,
    timestamps: List[int],
    readings: ReadingsType,
    units_in: UnitDictType,
    units_out: UnitDictType,
) -> Dict:
    data_idx = find_data_idx(fmt, ["energy", "power"])
    if not data_idx:
        raise data_type_not_found(
            "No element of {} found in format {}".format(["energy", "power"], fmt)
        )

    retval = {}
    retval["duration"] = get_duration(timestamps, units_in["time"]).convert(
        units_out["time"]
    )

    for skt_readings in readings:
        idle_readings = retval[skt_readings[dev_type]] = {}
        for loc, samples in {
            k: v for k, v in skt_readings.items() if k != dev_type
        }.items():
            if not samples:
                idle_readings[loc] = None
            else:
                if data_idx.type == "energy":
                    idle_readings[loc] = get_total_from_accumulated_energy(
                        samples, data_idx.idx, units_in[data_idx.type]
                    ).convert(units_out[data_idx.type])
                elif data_idx.type == "power":
                    time_idx = find_data_idx(fmt, ["sensor_time"])
                    if not time_idx:
                        print(
                            "IDLE: found power without sensor timestamps data ({})".format(
                                loc
                            ),
                            file=sys.stderr,
                        )
                        idle_readings[loc] = get_total_from_power_samples(
                            timestamps,
                            samples,
                            data_idx.idx,
                            (units_in["time"], units_in["power"]),
                            units_out["energy"],
                        )
                    else:
                        print(
                            "IDLE: found power with sensor timestamps data ({})".format(
                                loc
                            ),
                            file=sys.stderr,
                        )
                        idle_readings[loc] = get_total_from_timed_power_samples(
                            samples,
                            time_idx.idx,
                            data_idx.idx,
                            (units_in["time"], units_in["power"]),
                            units_out["energy"],
                        )
    return retval


def compute_delta(
    total_energy: units.Energy,
    total_duration: units.Time,
    idle_energy: units.Energy,
    idle_duration: units.Time,
) -> units.Energy:
    norm = idle_energy * (total_duration / idle_duration)
    if total_energy <= norm:
        return units.Energy(0)
    return total_energy - norm


def write_device_readings(
    fmt: FormatType,
    dev_type: str,
    units_in: UnitDictType,
    units_out: UnitDictType,
    readings_in,
    readings_out,
    sample_times: SampleTimes,
    idle,
):
    data_idx = find_data_idx(fmt, ["energy", "power"])
    if not data_idx:
        raise data_type_not_found(
            "No element of {} found in format {}".format(["energy", "power"], fmt)
        )
    for readings in readings_in:
        readings_out.append({})
        for k, v in readings.items():
            if k == dev_type:
                readings_out[-1][k] = v
            elif len(v) < 2:
                readings_out[-1][k] = None
            else:
                total_energy = None
                if data_idx.type == "energy":
                    print("found energy data ({})".format(k), file=sys.stderr)
                    total_energy = get_total_from_accumulated_energy(
                        v, data_idx.idx, units_in["energy"]
                    ).convert(units_out["energy"])
                elif data_idx.type == "power":
                    time_idx = find_data_idx(fmt, ["sensor_time"])
                    if not time_idx:
                        print(
                            "found power without sensor time data ({})".format(k),
                            file=sys.stderr,
                        )
                        total_energy = get_total_from_power_samples(
                            sample_times,
                            v,
                            data_idx.idx,
                            (units_in["time"], units_in["power"]),
                            units_out["energy"],
                        )
                    else:
                        print(
                            "found power with sensor time data ({})".format(k),
                            file=sys.stderr,
                        )
                        total_energy = get_total_from_timed_power_samples(
                            v,
                            time_idx.idx,
                            data_idx.idx,
                            (units_in["time"], units_in["power"]),
                            units_out["energy"],
                        )
                else:
                    raise AssertionError("Data is not 'energy' or 'power'")
                duration = get_duration(sample_times, units_in["time"]).convert(
                    units_out["time"]
                )
                delta_energy = (
                    compute_delta(
                        total_energy,
                        duration,
                        idle[readings[dev_type]][k],
                        idle["duration"],
                    ).convert(units_out["energy"])
                    if idle
                    else None
                )

                current = readings_out[-1][k] = {}
                current["total"] = total_energy.value
                current["delta"] = delta_energy.value if delta_energy else None


def _str_to_frac(sym: str, fracs: Dict[units.Fraction, str]) -> units.Fraction:
    for k, v in fracs.items():
        if v == sym:
            return k
    raise ValueError("Unsupported units found")


def get_units(raw: Dict[str, str]) -> UnitDictType:
    retval = {}
    type_map = {"energy": units.Energy, "power": units.Power, "time": units.Time}
    for utype, usym in raw.items():
        retval[utype] = _str_to_frac(usym, type_map[utype].units)
    return retval


def serialize_units(to_write: UnitDictType) -> Dict[str, str]:
    type_map = {"energy": units.Energy, "power": units.Power, "time": units.Time}
    return {utype: type_map[utype].units[frac] for utype, frac in to_write.items()}


def main():
    parser = argparse.ArgumentParser(
        description="Compact output and show total consumed energy"
    )
    add_arguments(parser)
    args = parser.parse_args()
    with read_from(args.source_file) as f:
        json_in = json.load(f)

        units_in = get_units(json_in["units"])
        units_out = copy.deepcopy(units_in)
        units_out["time"] = units.base

        dev_keys = {"cpu": "socket", "gpu": "device"}

        idle = {}
        if args.delta:
            for i in json_in["idle"]:
                for tgt, readings, dev_key in (
                    (k, i[k], v) for k, v in dev_keys.items() if i.get(k)
                ):
                    idle[tgt] = compute_idle(
                        json_in["format"][tgt],
                        dev_key,
                        i["sample_times"],
                        readings,
                        units_in,
                        units_out,
                    )

        json_out = {"groups": []}
        for g in json_in["groups"]:
            jgroup = {}
            jgroup["extra"] = g["extra"]
            jgroup["label"] = g["label"]
            jgroup["sections"] = []
            for s in g["sections"]:
                jsec = {}
                jsec["extra"] = s["extra"]
                jsec["label"] = s["label"]
                jsec["executions"] = []
                for e in s["executions"]:
                    jexec = {}
                    jexec["range"] = e["range"]
                    jexec["time"] = (
                        get_duration(e["sample_times"], units_in["time"])
                        .convert(units_out["time"])
                        .value
                    )
                    for tgt, readings, dev_key in (
                        (k, e[k], v) for k, v in dev_keys.items() if e.get(k)
                    ):
                        jexec[tgt] = []
                        write_device_readings(
                            json_in["format"][tgt],
                            dev_key,
                            units_in,
                            units_out,
                            readings,
                            jexec[tgt],
                            e["sample_times"],
                            idle[tgt] if idle else None,
                        )
                    jsec["executions"].append(jexec)
                jgroup["sections"].append(jsec)
            json_out["groups"].append(jgroup)
        del units_out["power"]
        json_out["units"] = serialize_units(units_out)

        with output_to(args.output) as of:
            json.dump(json_out, of)


if __name__ == "__main__":
    main()
