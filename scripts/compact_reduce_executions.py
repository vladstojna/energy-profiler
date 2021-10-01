#!/usr/bin/env python3

import argparse
import json
import sys
from typing import Any, Dict, List, Optional, Tuple, Union


def log(*args: Any) -> None:
    print("{}:".format(sys.argv[0]), *args, file=sys.stderr)


def read_from(path: str) -> Any:
    return sys.stdin if not path else open(path, "r")


def output_to(path: str) -> Any:
    return sys.stdout if not path else open(path, "w")


def add_arguments(parser: argparse.ArgumentParser) -> argparse.ArgumentParser:
    parser.add_argument(
        "source_file",
        action="store",
        help="file to extract from (default: stdin)",
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
        "--op",
        action="store",
        help="apply OPERATION to executions",
        metavar="OPERATION",
        required=True,
        type=str,
        choices=("sum", "max", "avg"),
    )
    return parser


Targets = Dict[str, str]
Readings = Dict[str, Union[Dict[str, float], int]]
Exec = Dict[str, Union[Dict[str, str], float, List[Readings]]]


def get_delta(delta: Optional[float]) -> float:
    return 0.0 if delta is None else delta


def reduce_max(execs: List[Exec]) -> Exec:
    retval: Optional[Exec] = next(iter(execs), None)
    if retval is None:
        raise AssertionError("Executions are empty")
    for e in execs[1:]:
        if e["time"] > retval["time"]:
            retval = e
    return retval


def reduce_sum(execs: List[Exec], targets: Targets) -> Exec:
    def _find_readings_by_dev(
        sensors: List[Readings], skt_readings: Readings, dev_key: str
    ) -> Readings:
        for s in sensors:
            if s[dev_key] == skt_readings[dev_key]:
                return s
        raise ValueError("{} {} not found".format(dev_key, skt_readings[dev_key]))

    multiple: str = "<multiple>"
    retval: Optional[Exec] = next(iter(execs), None)
    if retval is None:
        raise AssertionError("Executions are empty")
    for e in execs[1:]:
        if retval["range"]["start"] != e["range"]["start"]:
            retval["range"]["start"] = multiple
        if retval["range"]["end"] != e["range"]["end"]:
            retval["range"]["end"] = multiple
        retval["time"] += e["time"]
        for rsensors, dev_key, sensors in (
            (retval[t], targets[t], e[t]) for t in targets if e.get(t)
        ):
            for skt_readings in sensors:
                retval_readings = _find_readings_by_dev(rsensors, skt_readings, dev_key)
                for rvalues, values in (
                    (retval_readings[l], s)
                    for l, s in skt_readings.items()
                    if isinstance(s, dict)
                ):
                    rvalues["total"] += values["total"]
                    rvalues["delta"] = get_delta(rvalues["delta"]) + get_delta(
                        values["delta"]
                    )
    return retval


def reduce_avg(execs: List[Exec], targets: Targets) -> Exec:
    count = len(execs)
    retval = reduce_sum(execs, targets)
    retval["time"] /= count
    for sensors in (retval[t] for t in targets if retval.get(t)):
        for skt_readings in sensors:
            for values in (v for v in skt_readings.values() if isinstance(v, dict)):
                values["total"] /= count
                values["delta"] /= count
    return retval


def process_execs(op: str, execs: List[Any], targets: Tuple[str, str]) -> Exec:
    if op == "sum":
        return reduce_sum(execs, targets)
    if op == "avg":
        return reduce_avg(execs, targets)
    if op == "max":
        return reduce_max(execs)
    raise AssertionError("Invalid operation type {}".format(op))


def main():
    targets = {"cpu": "socket", "gpu": "device"}
    parser = argparse.ArgumentParser(description="Reduce executions")
    args = add_arguments(parser).parse_args()
    with read_from(args.source_file) as f:
        json_in = json.load(f)
        for g in json_in["groups"]:
            for s in g["sections"]:
                if s["executions"]:
                    s["executions"] = [process_execs(args.op, s["executions"], targets)]
        with output_to(args.output) as of:
            json.dump(json_in, of)


if __name__ == "__main__":
    main()
