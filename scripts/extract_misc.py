#!/usr/bin/env python3

import json
import sys


def get_file_handle():
    if len(sys.argv) == 1:
        return sys.stdin
    else:
        return open(sys.argv[-1], "r")


def get_devices(execution, target, attr):
    if not execution.get(target):
        return []
    return [d[attr] for d in execution[target]]


def join_value(lst):
    return "|".join(str(e) for e in lst) if lst else "none"


def main():
    with get_file_handle() as f:
        json_in = json.load(f)
        out = []
        formatstr = [
            "group",
            "section",
            "exec_count",
            "cpu_sockets",
            "gpu_devices",
        ]
        for g in json_in["groups"]:
            for s in g["sections"]:
                values = [g["label"], s["label"], len(s["executions"])]
                if s["executions"]:
                    values.append(
                        join_value(get_devices(s["executions"][0], "cpu", "socket"))
                    )
                    values.append(
                        join_value(get_devices(s["executions"][0], "gpu", "device"))
                    )
                else:
                    values.append("none")
                    values.append("none")
                if len(values) != len(formatstr):
                    raise AssertionError("Number of value entries do no match format")
                out.append(values)

        print("#{}".format(",".join(formatstr)))
        print("\n".join(",".join(str(k) for k in e) for e in out))


if __name__ == "__main__":
    main()
