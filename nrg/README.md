# NRG

## The Library

NRG (pronounced "energy") is a small helper library used in the energy profiler project as a way to
read the platform's energy/power sensors.
It is fully standalone and supports both x86_64 and POWER9 CPUs, and both NVIDIA and AMD GPUs.

## Prerequisites

On the GPU side, the library uses the following interfaces, depending on the GPU vendor:

* NVIDIA Management Library (NVML) - for reading GPU sensors on NVIDIA GPUs (Fermi and later)
* ROCm System Management Interface (SMI) - for reading GPU sensors on AMD GPUs (Vega 10 and later)

On the x86_64 CPU side, the library uses the Powercap interface in Linux. Powercap is available since
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
make -f Libs.mk lib/expected
```

Or (in the `nrg` directory):

```shell
make -C .. -f Libs.mk lib/expected
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

Other Make variables which can be overriden:

* `gpu=<value>`, where `<value>` can be:
  * `GPU_NV` - if using NVIDIA GPUs
  * `GPU_AMD` - if using AMD GPUs
  * `GPU_NONE` - requests do nothing; useful when, for example, the system has
    no dedicated GPU or the user is not interested in GPU results
* `cpu=<value>` where `<value>` can be:
  * `CPU_NONE` - requests do nothing; useful when, for example, the user is
    not interested in CPU results or does not have the required
    permissions to read the sensors
* `rocm_ver=<version>`, used when the ROCm installation path is versioned
  (no effect if `gpu` is not `GPU_AMD`)

Additionally, some options are provided as preprocessor definitions.
To enable them, use `make` with the `cpp` argument:

```shell
make cpp="MY_MACRO_DEFINITION"
```

Supported preprocessor definitions are:

* `NRG_X86_64` - force x86_64 code for testing purposes
* `NRG_PPC64` - force PowerPC64 code for testing purposes
* `NRG_OCC_USE_DUMMY_FILE=path` - use a custom path
  to the `occ_inband_sensors` file; has no effect unless using PowerPC64 systems or
  `NRG_PPC64` was provided
  (for testing purposes if the user has no appropriate permissions)
* `NRG_OCC_DEBUG_PRINTS` - prints all current sensor readings during initialisation

By default, both GPU and CPU vendors are autodetected.
The building procedure will create `libnrg.so` and/or `libnrg.a` in `lib`.

## Usage Examples

Basic example on x86_64:

```cpp
using namespace nrgprf;
try
{
  // construct reader of package sensors of socket 0
  reader_rapl reader{ locmask::pkg, 0x1 };
  sample smp;
  // read counter values into sample
  if (std::error_code ec; !reader.read(smp, ec))
  {
    std::cerr << ec.message() << "\n";
    return 1;
  }
  // get the package value read for socket 0
  auto energy = reader.value<loc::pkg>(smp, 0);
  if (!energy)
  {
      std::cerr << energy.error().message() << "\n";
      return 1;
  }
  std::cout << "Energy: " << joules<double>{ *energy }.count() << " J\n";
}
catch (const nrgprf::exception& e)
{
    std::cerr << e.what() << "\n";
    return 1;
}
```

The library interface changes depending on whether the target architecture is
x86_64 or PPC64 due to inherent differences in the available power/energy
interfaces. For example, RAPL (x86_64) counts the accumulated energy without
any timestamp data, while the OCC interfaces on POWER9 processors read the power
in Watts but provide timestamp information as well.

More examples can be found in `examples`, for both x86_64 and PPC64.

## Masks

### Socket & GPU Device

Socket and GPU device mask is straightforward: the non-zero bits represent
the respective CPU socket or GPU device. For example, `0x1` is socket/device 0.

### Sensor Mask

| Sensor Location |  Alias   | Value (hex) |   Platform   |
| --------------- | :------: | :---------: | :----------: |
| Package         |  `pkg`   |   `0x01`    | x86_64/PPC64 |
| Cores           | `cores`  |   `0x02`    | x86_64/PPC64 |
| Uncore          | `uncore` |   `0x04`    | x86_64/PPC64 |
| Memory          |  `mem`   |   `0x08`    | x86_64/PPC64 |
| System          |  `sys`   |   `0x10`    |    PPC64     |
| GPU             |  `gpu`   |   `0x20`    |    PPC64     |
| All             |  `all`   |   `0x3f`    |      -       |

The mask does not require valid values to work.
For example, `0xff` will be interpreted as `all` and invalid or non-existent
values will be ignored.

The definitions can be found [here](include/nrg/constants.hpp).
