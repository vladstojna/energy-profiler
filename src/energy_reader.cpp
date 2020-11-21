// energy_reader.cpp
#include "energy_reader.h"
#include "energy_reader_papi.h"
#include "energy_reader_pcm.h"

#include <cassert>

std::unique_ptr<tep::energy_reader> tep::make_energy_reader(
    tep::energy::target target,
    tep::energy::engine engine,
    size_t init_sample_count)
{
    switch (target)
    {
    case tep::energy::target::smp:
        switch (engine)
        {
        case tep::energy::engine::papi:
            return std::make_unique<tep::energy_reader_papi>(init_sample_count);
        case tep::energy::engine::pcm:
            return std::make_unique<tep::energy_reader_pcm>(init_sample_count);
        default:
            throw std::runtime_error("Unknown energy engine");
        }
    case tep::energy::target::gpu:
        throw std::logic_error("GPU reader not yet implemented");
    case tep::energy::target::fpga:
        throw std::logic_error("FPGA reader not yet implemented");
    default:
        throw std::runtime_error("Unknown energy target");
    }
    assert(false);
}
