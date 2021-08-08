#!/usr/bin/env python3

import json
import sys
import argparse
from typing import Any, Dict, Iterable, Sequence, Set


targets = {"cpu": "socket", "gpu": "device"}


def read_from(path):
    return sys.stdin if not path else open(path, "r")


def output_to(path):
    return sys.stdout if not path else open(path, "w")


def add_args(parser: argparse.ArgumentParser) -> None:
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
        "-t",
        "--target",
        action="store",
        help="which targets to filter (default: auto)",
        required=False,
        type=str,
        choices=[*targets, "all", "auto"],
        default="auto",
    )


def get_filters(target: str, fmt: Dict[str, Iterable[str]]) -> Dict[str, bool]:
    if target == "all":
        return dict.fromkeys(targets, True)
    if target in targets:
        retval = dict.fromkeys(targets, False)
        retval[target] = True
        return retval
    if target == "auto":
        retval = {}
        for k, v in fmt.items():
            if "energy" in v:
                retval[k] = True
            elif "power" in v:
                retval[k] = False
                for dt in v:
                    if "time" in dt:
                        retval[k] = True
        return retval
    raise ValueError("Invalid target '{}'".format(target))


def get_execution_with_target(
    execs: Iterable[Dict[str, Any]], target: str
) -> Dict[str, Any]:
    retval = None
    for i in execs:
        if not i.get(target) is None:
            retval = i
    return retval


def get_removal_set(
    execution: Dict[str, Any], sample_times: Iterable[int], dev_key: str
) -> Set[int]:
    remove_ix = set()
    # build the indices to remove as a union of all removals
    for sensors in execution:
        for samples in (v for k, v in sensors.items() if k != dev_key and v):
            assert len(sample_times) == len(samples)
            if len(sample_times) != len(samples):
                raise AssertionError("len(sample_times) != len(samples)")
            rm_ix = []
            for ix in range(1, len(samples)):
                if samples[ix] == samples[ix - 1]:
                    rm_ix.append(ix)
            if len(rm_ix) == len(samples) - 1:
                rm_ix.pop()
            remove_ix.update(rm_ix)
    return remove_ix


def remove_indices(
    remove_ix: Set[int],
    execution: Dict[str, Any],
    sample_times: Iterable[int],
    dev_key: str,
) -> None:
    for sensors in execution:
        for samples in (v for k, v in sensors.items() if k != dev_key and v):
            for ix in reversed(sorted(remove_ix)):
                del samples[ix]
            assert len(sample_times) == len(samples)


def log(fmt: str, *other: Sequence[Any]):
    print(fmt.format(*other), file=sys.stderr)


def filter_execution(e: Dict[str, Any], filters: Dict[str, bool], comment: str) -> None:
    sample_times = e["sample_times"]
    remove_ix = set()
    for tgt, readings in ((k, e[k]) for k, v in filters.items() if v and e.get(k)):
        remove_ix.update(get_removal_set(readings, sample_times, targets[tgt]))
        log("found:{}:{}={}", comment, tgt, remove_ix if remove_ix else "{}")
    if remove_ix:
        for ix in reversed(sorted(remove_ix)):
            del sample_times[ix]
        for tgt, rds in ((k, e[k]) for k, v in filters.items() if v and e.get(k)):
            remove_indices(remove_ix, rds, sample_times, targets[tgt])
            log("removed:{}:{}={}", comment, tgt, remove_ix)


def main():
    parser = argparse.ArgumentParser(description="Filter duplicate sensor readings")
    add_args(parser)
    args = parser.parse_args()
    with read_from(args.source_file) as f:
        json_in = json.load(f)
        filters = get_filters(args.target, json_in["format"])
        for e in json_in["idle"]:
            filter_execution(e, filters, "idle")
        for g in json_in["groups"]:
            for s in g["sections"]:
                for idx, e in enumerate(s["executions"]):
                    filter_execution(
                        e, filters, "{}:{}:{}".format(g["label"], s["label"], idx)
                    )
        with output_to(args.output) as of:
            json.dump(json_in, of)


if __name__ == "__main__":
    main()
