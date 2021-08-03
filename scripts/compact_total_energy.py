#!/usr/bin/env python3

import json
import sys
import copy
import argparse


def convert_units(from_unit, to_unit):
    if from_unit.unit_string == to_unit:
        return
    if from_unit.units.get(to_unit) == None:
        raise invalid_unit("Unit not supported")
    from_unit.value *= from_unit.multiplier / from_unit.units.get(to_unit)
    from_unit.multiplier = from_unit.units.get(to_unit)
    from_unit.unit_string = to_unit


class invalid_unit(Exception):
    def __init__(self, message) -> None:
        self.message = message


class data_type_not_found(Exception):
    def __init__(self, message) -> None:
        self.message = message


class scalar_unit:
    def __init__(self, value, multiplier, unit_string):
        self.value = value
        self.multiplier = multiplier
        self.unit_string = unit_string

    def __bool__(self):
        return self.value != 0

    def __str__(self):
        return str(self.value) + " " + self.unit_string

    def convert_to(self, to_unit):
        convert_units(self, to_unit)
        return self


class timestamp(scalar_unit):
    units = {"ns": 1e-9, "us": 1e-6, "ms": 1e-3, "s": 1}

    def __init__(self, value, units):
        if self.units.get(units) == None:
            raise invalid_unit("Unsupported unit " + units)
        super().__init__(value, self.units[units], units)


class energy(scalar_unit):
    units = {"nJ": 1e-9, "uJ": 1e-6, "mJ": 1e-3, "J": 1}

    def __init__(self, value, units):
        if self.units.get(units) == None:
            raise invalid_unit("Unsupported unit " + units)
        super().__init__(value, self.units[units], units)


class power(scalar_unit):
    units = {"nW": 1e-9, "uW": 1e-6, "mW": 1e-3, "W": 1}

    def __init__(self, value, units):
        if self.units.get(units) == None:
            raise invalid_unit("Unsupported unit " + units)
        super().__init__(value, self.units[units], units)


class data_idx:
    def __init__(self, type, idx):
        self.type = type
        self.idx = idx

    def __bool__(self):
        return self.idx != -1

    def __str__(self):
        return "(" + str(self.type) + "," + str(self.idx) + ")"


def read_from(path):
    if not path:
        return sys.stdin
    else:
        return open(path, "r")


def add_arguments(parser):
    parser.add_argument(
        "source_file",
        action="store",
        help="file to compact",
        nargs="?",
        type=str,
        default=None,
    )


def get_duration(sample_times, time_units):
    return timestamp(sample_times[-1] - sample_times[0], time_units)


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


def get_total_from_accumulated_energy(samples, idx, energy_units):
    return energy(samples[-1][idx] - samples[0][idx], energy_units)


def get_total_from_power_samples(timestamps, samples, idx, units_in, units_out):
    if len(timestamps) != len(samples):
        raise AssertionError("Length of samples and timestamps differ")
    energy_total = energy(0.0, units_out["energy"])
    for ix in range(1, len(timestamps)):
        avg_pwr = power(
            (samples[ix][idx] + samples[ix - 1][idx]) / 2, units_in["power"]
        ).convert_to(units_out["power"])
        duration = timestamp(
            timestamps[ix] - timestamps[ix - 1], units_in["time"]
        ).convert_to(units_out["time"])

        energy_total.value += avg_pwr.value * duration.value
    return energy_total


def get_total_from_timed_power_samples(
    samples, idx_time, idx_power, units_in, units_out
):
    energy_total = energy(0.0, units_out["energy"])
    for ix in range(1, len(samples)):
        avg_pwr = power(
            (samples[ix][idx_power] + samples[ix - 1][idx_power]) / 2, units_in["power"]
        ).convert_to(units_out["power"])
        duration = timestamp(
            samples[ix][idx_time] - samples[ix - 1][idx_time], units_in["time"]
        ).convert_to(units_out["time"])

        energy_total.value += avg_pwr.value * duration.value
    return energy_total


def compute_idle(fmt, dev_type, timestamps, readings, units_in, units_out):
    data_idx = find_data_idx(fmt, ["energy", "power"])
    if not data_idx:
        raise data_type_not_found(
            "No element of " + str(["energy", "power"]) + " found in format " + str(fmt)
        )

    retval = {}
    retval["duration"] = get_duration(timestamps, units_in["time"]).convert_to(
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
                    ).convert_to(units_out[data_idx.type])
                elif data_idx.type == "power":
                    time_idx = find_data_idx(fmt, ["sensor_time"])
                    if not time_idx:
                        print(
                            "IDLE: found power without sensor timestamps data",
                            file=sys.stderr,
                        )
                        idle_readings[loc] = get_total_from_power_samples(
                            timestamps,
                            samples,
                            data_idx.idx,
                            units_in,
                            units_out,
                        ).convert_to(units_out["energy"])
                    else:
                        print(
                            "IDLE: found power with sensor timestamps data",
                            file=sys.stderr,
                        )
                        idle_readings[loc] = get_total_from_timed_power_samples(
                            samples,
                            time_idx.idx,
                            data_idx.idx,
                            units_in,
                            units_out,
                        ).convert_to(units_out["energy"])
    return retval


def compute_delta(total_energy, total_duration, idle_energy, idle_duration):
    if total_energy.unit_string != idle_energy.unit_string:
        idle_energy.convert_to(total_energy.unit_string)
    norm = idle_energy.value * (total_duration.value / idle_duration.value)
    if total_energy.value <= norm:
        return energy(0, total_energy.unit_string)
    return energy(total_energy.value - norm, total_energy.unit_string)


def write_device_readings(
    fmt, dev_type, units_in, units_out, readings_in, readings_out, sample_times, idle
):
    data_idx = find_data_idx(fmt, ["energy", "power"])
    if not data_idx:
        raise data_type_not_found(
            "No element of " + str(["energy", "power"]) + " found in format " + str(fmt)
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
                    print("found energy data", file=sys.stderr)
                    total_energy = get_total_from_accumulated_energy(
                        v, data_idx.idx, units_in["energy"]
                    ).convert_to(units_out["energy"])
                elif data_idx.type == "power":
                    time_idx = find_data_idx(fmt, ["sensor_time"])
                    if not time_idx:
                        print("found power without sensor time data", file=sys.stderr)
                        total_energy = get_total_from_power_samples(
                            sample_times,
                            v,
                            data_idx.idx,
                            units_in,
                            units_out,
                        ).convert_to(units_out["energy"])
                    else:
                        print("found power with sensor time data", file=sys.stderr)
                        total_energy = get_total_from_timed_power_samples(
                            v,
                            time_idx.idx,
                            data_idx.idx,
                            units_in,
                            units_out,
                        ).convert_to(units_out["energy"])
                else:
                    raise AssertionError("Data is not 'energy' or 'power'")
                duration = get_duration(sample_times, units_in["time"]).convert_to(
                    units_out["time"]
                )
                delta_energy = compute_delta(
                    total_energy,
                    duration,
                    idle[readings[dev_type]][k],
                    idle["duration"],
                ).convert_to(units_out["energy"])

                current = readings_out[-1][k] = {}
                current["total"] = total_energy.value
                current["delta"] = delta_energy.value if delta_energy else None


def main():
    parser = argparse.ArgumentParser(
        description="Compact output and show total consumed energy"
    )
    add_arguments(parser)
    args = parser.parse_args()
    with read_from(args.source_file) as f:
        json_in = json.load(f)
        units = json_in["units"]
        units_out = copy.deepcopy(units)
        units_out["time"] = "s"

        idle = {}
        for i in json_in["idle"]:
            if i.get("cpu") != None:
                idle["cpu"] = compute_idle(
                    json_in["format"]["cpu"],
                    "socket",
                    i["sample_times"],
                    i["cpu"],
                    units,
                    units_out,
                )
            if i.get("gpu") != None:
                idle["gpu"] = compute_idle(
                    json_in["format"]["gpu"],
                    "device",
                    i["sample_times"],
                    i["gpu"],
                    units,
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
                        get_duration(e["sample_times"], units["time"])
                        .convert_to(units_out["time"])
                        .value
                    )
                    if e.get("cpu") != None:
                        jexec["cpu"] = []
                        write_device_readings(
                            json_in["format"]["cpu"],
                            "socket",
                            units,
                            units_out,
                            e["cpu"],
                            jexec["cpu"],
                            e["sample_times"],
                            idle["cpu"],
                        )

                    if e.get("gpu") != None:
                        jexec["gpu"] = []
                        write_device_readings(
                            json_in["format"]["gpu"],
                            "device",
                            units,
                            units_out,
                            e["gpu"],
                            jexec["gpu"],
                            e["sample_times"],
                            idle["gpu"],
                        )

                    jsec["executions"].append(jexec)
                jgroup["sections"].append(jsec)
            json_out["groups"].append(jgroup)
        del units_out["power"]
        json_out["units"] = units_out
        json.dump(json_out, sys.stdout)


if __name__ == "__main__":
    main()
