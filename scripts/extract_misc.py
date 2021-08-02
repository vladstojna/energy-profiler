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


def main():
    with get_file_handle() as f:
        json_in = json.load(f)
        out = []
        for g in json_in["groups"]:
            for s in g["sections"]:
                values = [g["label"], s["label"], len(s["executions"])]
                if s["executions"]:
                    cpu_sockets = get_devices(s["executions"][0], "cpu", "socket")
                    gpu_devices = get_devices(s["executions"][0], "gpu", "device")
                    values.append(
                        "|".join(str(e) for e in cpu_sockets) if cpu_sockets else "none"
                    )
                    values.append(
                        "|".join(str(e) for e in gpu_devices) if gpu_devices else "none"
                    )
                else:
                    values.append("none")
                    values.append("none")
                out.append(values)

        print("#group,section,exec_count,cpu_sockets,gpu_devices")
        print("\n".join(",".join(str(k) for k in e) for e in out))


if __name__ == "__main__":
    main()
