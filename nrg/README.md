# NRG

## The Library

NRG (pronounced "energy") is a small helper library used in the energy profiler project as a way to
read the platform's energy/power sensors.
It is fully standalone and supports both x86 and POWER9 CPUs, and both NVIDIA and AMD GPUs.

## Prerequisites

On the GPU side, the library uses the following interfaces, depending on the GPU vendor:

* NVIDIA Management Library (NVML) - for reading GPU sensors on NVIDIA GPUs (Fermi and later)
* ROCm System Management Interface (SMI) - for reading GPU sensors on AMD GPUs (Vega 10 and later)

On the CPU side, the library uses the Powercap interface in Linux on x86. Powercap is available since
Linux 3.13 on Intel CPUs and since Linux 5.11 on AMD Zen CPUs.
Make sure the files in `/sys/class/powercap` have appropriate read permissions.
The POWER9 implementation uses the OCC sensor interfaces
([specification](https://github.com/open-power/docs/blob/master/occ/OCC_P9_FW_Interfaces.pdf)
starting on page 142).
Make sure the file `/sys/firmware/opal/exports/occ_inband_sensors` has appropriate read permissions.

## Building the Library

Before building the library, prepare the required dependency
by running (in the top-level directory):

```shell
make lib/expected
```

Or (in the current directory):

```shell
make -c .. lib/expected
```

Build the library (dynamic), execute:

```shell
make
```

Or:

```shell
make dynamic
```

Build the static variant:

```shell
make static
```

Generate a debug build:

```shell
make dbg=1
```

**TODO** preprocessor definitions

## Usage Examples

Basic example of using the library on x86:
**TODO**
