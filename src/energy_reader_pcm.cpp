// energy_reader_pcm.cpp
#include "energy_reader_pcm.h"

#include <cassert>

tep::energy_reader_pcm::sample_point::sample_point(const timepoint_t& tp,
    uint32_t num_skts,
    std::vector<pcm::CoreCounterState>& ccs) :
    basic_sample(tp),
    system_state(),
    socket_states(),
    core_dummy_states(ccs)
{
    socket_states.reserve(num_skts);
}

tep::energy_reader_pcm::energy_reader_pcm(size_t init_sample_count) :
    energy_reader(),
    _samples(),
    _dummy_states(),
    _pcm(nullptr)
{
    _pcm = pcm::PCM::getInstance();
    pcm::PCM::ErrorCode status = _pcm->program();
    switch (status)
    {
    case pcm::PCM::Success:
        break;
    case pcm::PCM::MSRAccessDenied:
        throw tep::energy_reader_exception(
            "Access to Processor Counter Monitor has denied"
            "(no MSR or PCI CFG space access)");
    case pcm::PCM::PMUBusy:
        throw tep::energy_reader_exception(
            "Access to Processor Counter Monitor has denied"
            "(Performance Monitoring Unit is occupied by other application)."
            "Try to stop the application that uses PMU.");
    default:
        throw tep::energy_reader_exception(
            "Access to Processor Counter Monitor has denied (Unknown error).");
    }
    _samples.reserve(init_sample_count);
}

tep::energy_reader_pcm::energy_reader_pcm(energy_reader_pcm&& other) :
    _samples(std::move(other._samples)),
    _dummy_states(std::move(other._dummy_states)),
    _pcm(std::exchange(other._pcm, nullptr))
{
}

tep::energy_reader_pcm::~energy_reader_pcm()
{
    if (_pcm != nullptr)
        _pcm->cleanup();
}

void tep::energy_reader_pcm::start()
{
    assert(_samples.size() == 0);
    emplace_write_counters();
}

void tep::energy_reader_pcm::sample()
{
    assert(_samples.size() > 0);
    emplace_write_counters();
}

void tep::energy_reader_pcm::stop()
{
    assert(_samples.size() > 0);
    emplace_write_counters();
}

void tep::energy_reader_pcm::print(std::ostream& os) const
{
    // there are always at least 2 samples
    assert(_pcm != nullptr);
    assert(_samples.size() > 1);
    os << "# results\n";
    constexpr const double unable_to_read = 0;
    for (size_t ix = 1; ix < _samples.size(); ix++)
    {
        auto const& sample_prev = _samples[ix - 1];
        auto const& sample = _samples[ix];
        std::stringstream buffer;
        buffer << ix - 1 << ',' << (sample - sample_prev).count();
        for (pcm::uint32 skt = 0; skt < _pcm->getNumSockets(); skt++)
        {
            double cpu = _pcm->packageEnergyMetricsAvailable() ?
                pcm::getConsumedJoules(
                    sample_prev.socket_states[skt],
                    sample.socket_states[skt])
                : unable_to_read;
            double dram = _pcm->dramEnergyMetricsAvailable() ?
                pcm::getDRAMConsumedJoules(
                    sample_prev.socket_states[skt],
                    sample.socket_states[skt])
                : unable_to_read;
            buffer << ',' << cpu << ',' << dram;
        }
        buffer << '\n';
        os << buffer.rdbuf();
    }
}

void tep::energy_reader_pcm::emplace_write_counters()
{
    assert(_pcm != nullptr);
    auto& sample = _samples.emplace_back(now(),
        _pcm->getNumSockets(), _dummy_states);
    _pcm->getAllCounterStates(
        sample.system_state,
        sample.socket_states,
        sample.core_dummy_states);
}
