# Energy Profiler

## The Tool

## Building the Profiler

### Requirements

* `libdwarf` - for source code line profiling (optional)
* `wget` - for downloading the necessary dependencies
* `cmake` - for building the necessary dependencies
* `gcc` - any version with C++17 support or later (tested with 10.1 and later)
* `nrg` - for reading the energy/power sensors

To build the profiler, execute:

```shell
make
```

To generate a debug build, execute:

```shell
make DEBUG=1
```

Additionally, some options are provided as preprocessor definitions.
To enable them, use `make` with the `cpp` argument:

```shell
make cpp="MY_MACRO_DEFINITION"
```

Supported preprocessor definitions are:

* `TEP_USE_LIBDWARF` - enable support for source line profiling (requires `libdwarf`)
* `NO_ASLR` - disable ASLR for the target executable

Running `make` will create an executable `profiler` in `bin`.

## Examples

### Configuration File

Example of a configuration file used for profiling the main function:

```xml
<config>
    <sections>
        <!-- read from the CPU energy/power interfaces -->
        <section target="cpu">
            <bounds>
                <!-- measure the 'main' function -->
                <func name="main"/>
            </bounds>
            <!-- allow execution of other threads during the profiling -->
            <allow_concurrency/>
            <!-- save all samples, useful for plotting -->
            <!-- other method is 'total', which only saves the first and last samples -->
            <method>profile</method>
            <!-- sample every 100 ms -->
            <interval>100</interval>
        </section>
    </sections>
</config>
```

More examples available in `examples/config`

Output example (some information omitted for clarity):

```json
{
    "format": {
        "cpu": [
            "energy"
        ],
        "gpu": [
            "energy"
        ]
    },
    "groups": [
        {
            "extra": null,
            "label": null,
            "sections": [
                {
                    "executions": [
                        {
                            "cpu": [
                                {
                                    "package": [
                                        [
                                            2157.485685
                                        ],
                                        [
                                            2158.552906
                                        ],
                                        [
                                            2159.745929
                                        ]
                                    ],
                                    "socket": 0
                                }
                            ],
                            "range": {
                                "end": "main+0x123",
                                "start": "main"
                            },
                            "sample_times": [
                                1633599496065031147,
                                1633599496165167837,
                                1633599496265385472
                            ]
                        }
                    ],
                    "extra": null,
                    "label": null
                }
            ]
        }
    ],
    "idle": [],
    "units": {
        "energy": "J",
        "power": "W",
        "time": "ns"
    }
}
```

## Running the Profiler

```shell
$ profiler --help
Usage:

profiler <options> [--] <executable>

options:
  -h, --help                    print this message and exit
  -c, --config <file>           (optional) read from configuration file <file>; if <file> is 'stdin' then stdin is used (default: stdin)
  -o, --output <file>           (optional) write profiling results to <file>; if <file> is 'stdout' then stdout is used (default: stdout)
  -q, --quiet                   suppress log messages except errors to stderr (default: off)
  -l, --log <file>              (optional) write log to <file> (default: stdout)
  --idle                        gather idle readings at startup (default)
  --no-idle                     opposite of --idle
  --cpu-sensors {MASK,all}      mask of CPU sensors to read in hexadecimal, overwrites config value (default: use value in config)
  --cpu-sockets {MASK,all}      mask of CPU sockets to profile in hexadecimal, overwrites config value (default: use value in config)
  --gpu-devices {MASK,all}      mask of GPU devices to profile in hexadecimal, overwrites config value (default: use value in config)
  --exec <path>                 evaluate executable <path> instead of <executable>; used when <executable> is some wrapper program which launches <path> (default: <executable>)
```

Example of running the profiler for socket 0 (`--cpu-sockets`),
reading the CPU package counters (`--cpu-sensors`):

```shell
./profiler --cpu-sockets 0 --cpu-sensors 1 --output my-output.json --config my-config.xml -- [executable]
```

Example of running the profiler in a multi-socket system with forced affinity using `numactl`:

```shell
$ my_exec=path/to/my/application
$ ./profiler \
    --cpu-sockets 0 \
    --cpu-sensors 1 \
    --output my-output.json \
    --config my-config.xml \
    --exec $my_exec \
    -- numactl --cpunodebind=0 --physcpubind=3 --membind=0 $my_exec [arguments]
```

### Additional Information

More information (like sensor masks) can be found in `nrg/README.md`
