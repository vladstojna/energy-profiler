# Energy Profiler

## The Tool

The energy profiler is a tool which measures the energy/power consumption of the
function(s) or code sections of a program.
On the CPU side, it supports both the x86_64 and POWER9 (powerpc64) architectures
and uses their respective platform sensors to obtain energy/power data.
On the GPU side, it supports NVIDIA and AMD GPUs, although the latter has not
been properly tested at the time of writing.
Support for FPGAs is still being looked into.

The general usage procedure is as follows:

1. Compile the target executable with debugging symbols.
2. Write a configuration file for the profiler in XML format.
3. Use said XML file as input for the profiler, alongside the executable to profile.
4. Analyse the output JSON file.

### Components

This project can be divided into three components:

* The profiler application
* The library and API for reading the power/energy sensors (located in `nrg`)
* A suite of Python scripts to aid with the post-processing of results (located in `scripts`)

## Prerequisites

* `wget` - for downloading the necessary dependencies
* `cmake` - for building the necessary dependencies
* `gcc` - any version with C++17 support or later (tested with 10.1 and later)
* `elfutils` (`libelf` and `libdw`) - used to gather ELF and DWARF information
* `libnrg` - for reading the energy/power sensors (located in `nrg`)

Make sure to build [`libnrg`](nrg/README.md) before proceeding further.

## Building the Profiler

Build the dependency libraries:

```shell
make -f Libs.mk
```

Build the profiler:

```shell
make
```

Generate a debug build:

```shell
make DEBUG=1
```

Other Make variables which can be overriden:

* `system_clock` - use the system's clock instead of a steady clock for
  timestamps; use when an absolute, real time is necessary

The building procedure will generate an executable `profiler` in `bin`.

## Examples

Configuration file used for profiling the `hello` function:

```xml
<config>
    <sections>
        <!-- read from the CPU energy/power interfaces -->
        <section target="cpu">
            <bounds>
                <!-- measure the 'hello' function -->
                <func name="hello"/>
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

The `method` tag is required and can be either **total** or
**profile**. This tag changes the profiling behaviour as described in a
comment above and, depending on its value, allows other attributes to be provided.
For example, when `method` is **profile**
then `interval` or `freq` must be provided (other tags like `samples` and
`duration` can be provided as well).
When `method` is **total** then `interval` becomes an implementation-defined value
and the `short` tag can be provided. Method-specific tags are ignored whenever
the `method` value is different from the expected one.
More examples with comments available in `examples/config`

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
                                "start": {
                                    "address": "0x200",
                                    "compilation_unit": {
                                        "path": "main.cpp"
                                    },
                                    "function_call": {
                                        "function": {
                                            "static": false,
                                            "declared": {
                                                "column": 6,
                                                "file": "main.cpp",
                                                "line": 10
                                            }
                                        },
                                        "symbol": {
                                            "binding": "global",
                                            "address": "0x200",
                                            "local_entrypoint": "0x200",
                                            "size": "0x20",
                                            "mangled_name": "_Z5hellov",
                                            "demangled_name": "hello()"
                                        }
                                    }
                                },
                                "end": {
                                    "function_return": true,
                                    "absolute_address": "0x323"
                                }
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
  --debug-dump <file>           (optional) dump gathered debug info in JSON format to <file>
  --idle                        gather idle readings at startup
  --no-idle                     do not gather idle readings at startup (default)
  --cpu-sensors {MASK,all}      mask of CPU sensors to read in hexadecimal, overwrites config value (default: use value in config)
  --cpu-sockets {MASK,all}      mask of CPU sockets to profile in hexadecimal, overwrites config value (default: use value in config)
  --gpu-devices {MASK,all}      mask of GPU devices to profile in hexadecimal, overwrites config value (default: use value in config)
  --exec <path>                 evaluate executable <path> instead of <executable>; used when <executable> is some wrapper program which launches <path> (default: <executable>)
  --enable-randomization        enable Address Space Layout Randomization (ASLR) for the target application
```

Example of running the profiler for socket 0 (`--cpu-sockets`),
reading the CPU package counters (`--cpu-sensors`):

```shell
./profiler --cpu-sockets 1 --cpu-sensors 1 --output my-output.json --config my-config.xml -- [executable]
```

Example of running the profiler in a multi-socket system with forced affinity using `numactl`:

```shell
$ my_exec=path/to/my/application
$ ./profiler \
    --cpu-sockets 1 \
    --cpu-sensors 1 \
    --output my-output.json \
    --config my-config.xml \
    --exec "$my_exec" \
    -- numactl --cpunodebind=0 --physcpubind=3 --membind=0 "$my_exec" [arguments]
```

## Limitations

The profiler does not yet support profiling:

* Sections/functions which require inter-thread communication during their execution.
  This is only the case when `<allow_concurrency/>` is omitted in the configuration file.
* Nested evaluated sections: one or more sections to be profiled inside another
  section to be profiled.
* Functions which have been inlined non-contiguously by the compiler into multiple
  contiguous ranges and interleaved with other code.

## Additional Information

More information (like sensor masks) can be found in [`nrg/README.md`](nrg/README.md)
