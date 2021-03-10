// energy_reader.cpp
#include "energy_reader.h"
#include "energy_reader_smp.h"
#include "energy_reader_pcm.h"
#include "energy_reader_gpu.h"

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
            return std::make_unique<tep::energy_reader_smp>(init_sample_count);
        case tep::energy::engine::pcm:
            return std::make_unique<tep::energy_reader_pcm>(init_sample_count);
        default:
            throw std::runtime_error("Unknown SMP energy engine");
        }
    case tep::energy::target::gpu:
        return std::make_unique<tep::energy_reader_gpu>(init_sample_count);
    case tep::energy::target::fpga:
        throw std::logic_error("FPGA reader not yet implemented");
    default:
        throw std::runtime_error("Unknown energy target");
    }
    assert(false);
}
