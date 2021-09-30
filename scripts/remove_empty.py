#!/usr/bin/env python3

import json
import sys
import argparse
from typing import Any


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


def main():
    targets = ("cpu", "gpu")

    parser = argparse.ArgumentParser(
        description="Remove empty entries from profiler output"
    )
    args = add_arguments(parser).parse_args()
    with read_from(args.source_file) as f:
        json_in = json.load(f)

        groups = json_in["groups"]
        g_to_keep = []
        for g in groups:
            g_lbl = g["label"]
            sections = g["sections"]
            if not sections and not args.keep_group:
                log("remove {}".format(g_lbl))
            else:
                g_to_keep.append(g)
                s_to_keep = []
                for s in sections:
                    s_lbl = s["label"]
                    execs = s["executions"]
                    if not execs and not args.keep_section:
                        log("remove {}:{}".format(g_lbl, s_lbl))
                    else:
                        s_to_keep.append(s)
                        e_to_keep = []
                        for eix, e in enumerate(execs):
                            # empty sample_times means there are no readings
                            if not e["sample_times"]:
                                log("remove {}:{}:{}".format(g_lbl, s_lbl, eix))
                            else:
                                e_to_keep.append(e)
                                for tgt, sensors in (
                                    (t, e[t]) for t in targets if e.get(t)
                                ):
                                    skt_readings_to_remove = []
                                    for skt_readings in sensors:
                                        for loc, samples in (
                                            (l, s)
                                            for l, s in skt_readings.items()
                                            if isinstance(s, list)
                                        ):
                                            if not samples and not args.keep_location:
                                                skt_readings_to_remove.append(loc)
                                    for rm in skt_readings_to_remove:
                                        del skt_readings[rm]
                                        log(
                                            "remove {}:{}:{}:{}:{}".format(
                                                g_lbl, s_lbl, eix, tgt, loc
                                            )
                                        )
                        s["executions"] = e_to_keep

                if not args.keep_section:
                    g["sections"] = s_to_keep

        if not args.keep_group:
            json_in["groups"] = g_to_keep

        with output_to(args.output) as of:
            json.dump(json_in, of)


if __name__ == "__main__":
    main()
