#!/usr/bin/env python3

import json
import sys
import argparse
from typing import Any, Dict, Generator, List, Tuple


def log(*args: Any) -> None:
    print("{}:".format(sys.argv[0]), *args, file=sys.stderr)


def read_from(path: str) -> Any:
    return sys.stdin if not path else open(path, "r")


def output_to(path: str) -> Any:
    return sys.stdout if not path else open(path, "w")


def add_arguments(parser: argparse.ArgumentParser) -> argparse.ArgumentParser:
    def _add_bool_flag(arg: str, obj_type: str) -> None:
        parser.add_argument(
            arg,
            action="store_true",
            help=f"keep {obj_type} objects even if empty",
            required=False,
            default=False,
        )

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
    _add_bool_flag("--keep-group", "group")
    _add_bool_flag("--keep-section", "section")
    _add_bool_flag("--keep-location", "location")
    return parser


def process_executions(
    execs: Any, g_lbl: str, s_lbl: str, targets: Tuple[str, str], keep_location: bool
) -> List[Any]:
    def get_socketORdevice(skt_readings: Dict) -> int:
        return next(
            filter(lambda x: not isinstance(x, list), skt_readings.values()), None
        )

    def readings_generator(skt_readings: Dict) -> Generator:
        return ((l, s) for l, s in skt_readings.items() if isinstance(s, list))

    e_to_keep = []
    for eix, e in enumerate(execs):
        # empty sample_times means there are no readings
        if not e["sample_times"]:
            log("remove {}:{}:{}".format(g_lbl, s_lbl, eix))
        else:
            e_to_keep.append(e)
            for tgt, sensors in ((t, e[t]) for t in targets if e.get(t)):
                for skt_readings in sensors:
                    dev = get_socketORdevice(skt_readings)
                    skt_readings_to_remove = []
                    for loc, samples in readings_generator(skt_readings):
                        if not samples and not keep_location:
                            skt_readings_to_remove.append(loc)
                    for rm in skt_readings_to_remove:
                        del skt_readings[rm]
                        args = g_lbl, s_lbl, eix, tgt, dev, rm
                        log("remove {}:{}:{}:{}:{}:{}".format(*args))
    return e_to_keep


def main():
    targets = ("cpu", "gpu")

    parser = argparse.ArgumentParser(
        description="Remove empty entries from profiler output"
    )
    args = add_arguments(parser).parse_args()
    with read_from(args.source_file) as f:
        json_in = json.load(f)

        process_executions(json_in["idle"], "idle", "", targets, args.keep_location)
        g_to_keep = []
        for g in json_in["groups"]:
            g_lbl = g["label"]
            s_to_keep = []
            for s in g["sections"]:
                s_lbl = s["label"]
                s["executions"] = process_executions(
                    s["executions"], g_lbl, s_lbl, targets, args.keep_location
                )
                if s["executions"] or args.keep_section:
                    s_to_keep.append(s)
                else:
                    log("remove {}:{}".format(g_lbl, s_lbl))

            if not args.keep_section:
                g["sections"] = s_to_keep

            if g["sections"] or args.keep_group:
                g_to_keep.append(g)
            else:
                log("remove {}".format(g_lbl))

        if not args.keep_group:
            json_in["groups"] = g_to_keep

        with output_to(args.output) as of:
            json.dump(json_in, of)


if __name__ == "__main__":
    main()
