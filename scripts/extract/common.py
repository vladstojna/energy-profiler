import sys
import argparse
from typing import Any, Iterable, List, Optional, Union


target_choices = {"cpu": "socket", "gpu": "device"}


class intlist_or_all(argparse.Action):
    def __init__(
        self,
        option_strings,
        dest=None,
        nargs="+",
        **kwargs,
    ) -> None:
        super().__init__(option_strings, dest=dest, nargs=nargs, **kwargs)

    def __call__(
        self,
        parser,
        namespace,
        values,
        option_string,
    ) -> None:
        if len(values) == 1 and next(iter(values)).lower() == "all":
            setattr(namespace, self.dest, "all")
        else:
            try:
                setattr(namespace, self.dest, sorted([int(e) for e in values]))
            except ValueError:
                raise argparse.ArgumentError(
                    self, "values must be base 10 integers or 'all'"
                )


def read_from(path: Optional[str]) -> Any:
    return sys.stdin if not path else open(path, "r")


def output_to(path: Optional[str]) -> Any:
    return sys.stdout if not path else open(path, "w")


def add_arguments(parser: argparse.ArgumentParser) -> argparse.ArgumentParser:
    parser.add_argument(
        "-g",
        "--group",
        action="store",
        help="group label (default: first found)",
        required=False,
        type=str,
        default=None,
    )
    parser.add_argument(
        "-s",
        "--section",
        action="store",
        help="section label (default: first found)",
        required=False,
        type=str,
        default=None,
    )
    parser.add_argument(
        "-e",
        "--execs",
        "--executions",
        action=intlist_or_all,
        help="execution index(es) or 'all' (default: all)",
        required=False,
        nargs="+",
        default="all",
    )
    parser.add_argument(
        "-t",
        "--target",
        action="store",
        help="hardware device target (default: first found)",
        required=False,
        type=str,
        choices=target_choices,
        default=None,
    )
    parser.add_argument(
        "-d",
        "--devs",
        "--devices",
        action=intlist_or_all,
        help="device/socket index(es) or 'all' (default: all)",
        required=False,
        nargs="+",
        default="all",
    )
    parser.add_argument(
        "-l",
        "--location",
        action="store",
        help="sensor location (default: all)",
        required=False,
        type=str,
        default=None,
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
    return parser


def find(json_in: Any, arg: Any, attr: str, attr_compare: str) -> Any:
    if not json_in.get(attr):
        raise AssertionError("attribute '{}' not found".format(attr))
    if not json_in[attr]:
        raise AssertionError("'{}' empty".format(attr))
    if not arg:
        return json_in[attr][0]
    else:
        retval = next((v for v in json_in[attr] if v[attr_compare] == arg), None)
        if not retval:
            raise ValueError(
                "{} '{}' not found in '{}'".format(attr_compare, arg, attr)
            )
        return retval


def get_executions(execs: List, idxs: Union[str, List[int]]) -> Iterable:
    if idxs == "all":
        return execs
    try:
        retval = []
        for ix in idxs:
            retval.append(execs[ix])
        return retval
    except IndexError:
        raise ValueError("Execution with index '{}' does not exist".format(ix))


def find_devices(
    readings: Iterable, to_find: Union[Iterable, str], dev_key: str
) -> list:
    retval = sorted(
        [x[dev_key] for x in readings if to_find == "all" or x[dev_key] in to_find]
    )
    if to_find != "all":
        for d in to_find:
            if d not in retval:
                raise ValueError("{} {} does not exist in readings".format(dev_key, d))
    return retval


def initial_exec_lookup(inout_args: Any, json_in: Any) -> List:
    group = find(json_in, inout_args.group, "groups", "label")
    section = find(group, inout_args.section, "sections", "label")
    if not inout_args.group:
        inout_args.group = group["label"]
    if not inout_args.section:
        inout_args.section = section["label"]

    if not section["executions"]:
        raise ValueError("Execution is empty")
    if not inout_args.execs:
        inout_args.execs.append(0)
    execs = get_executions(section["executions"], inout_args.execs)
    if not execs:
        raise AssertionError("execs has no elements")
    if inout_args.execs == "all":
        inout_args.execs = [x for x in range(len(execs))]
    if len(execs) != len(inout_args.execs):
        raise AssertionError("exec count != indices requested")

    if not inout_args.target:
        for k in next(iter(execs)):
            if k in target_choices:
                inout_args.target = k
                break
    return execs
