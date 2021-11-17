# NRG

## The Library

NRG (pronounced "energy") is a small helper library used in the energy profiler project as a way to
read the platform's energy/power sensors.
It is fully standalone and supports both x86_64 and POWER9 CPUs, and both NVIDIA and AMD GPUs.

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

Basic example on x86:

```cpp
// construct reader of package sensors of socket 0
reader_rapl reader(locmask::pkg, 0x1, err);
if (err)
{
    std::cerr << err << "\n";
    return 1;
}
sample smp;
if (auto err = reader.read(smp))
{
    std::cerr << err << "\n";
    return 1;
}
// get the package value read for socket 0
auto energy = reader.value<loc::pkg>(smp, 0);
if (!energy)
{
    std::cerr << energy.error() << "\n";
    return 1;
}
std::cout << "Energy: " << joules<double>{ *energy }.count() << " J\n";
```

The library interface changes depending on whether the target architecture is
x86_64 or PPC64 due to inherent differences in the available power/energy
interfaces. For example, RAPL (x86_64) counts the accumulated energy without
any timestamp data, while the OCC interfaces on POWER9 processors read the power
in Watts but provide timestamp information as well.

More examples can be found in `examples`, for both x86_64 and PPC64.
