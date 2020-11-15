// energy_reader_pcm.cpp
#include "energy_reader_pcm.h"

#include <cassert>

tep::energy_reader_pcm::sample_point::sample_point(
    uint64_t count,
    uint32_t num_skts,
    std::vector<pcm::CoreCounterState>& ccs) :
    number(count),
    system_state(),
    socket_states(),
    core_dummy_states(ccs)
{
    socket_states.reserve(num_skts);
}

tep::energy_reader_pcm::energy_reader_pcm() :
    energy_reader(),
    _samples(),
    _dummy_states(),
    _pcm(nullptr)
{
    constexpr const size_t initial_size = 64;
    _pcm = pcm::PCM::getInstance();
    _samples.reserve(initial_size);
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
}

tep::energy_reader_pcm::energy_reader_pcm(energy_reader_pcm&& other) :
    _samples(std::move(other._samples)),
    _dummy_states(std::move(other._dummy_states)),
    _pcm(std::exchange(other._pcm, nullptr))
{
}

void tep::energy_reader_pcm::sample()
{
    assert(_pcm != nullptr);
    _samples.emplace_back(_sample_count, _pcm->getNumSockets(), _dummy_states);
    auto& sample = _samples.back();
    _pcm->getAllCounterStates(
        sample.system_state,
        sample.socket_states,
        sample.core_dummy_states);
    _sample_count++;
}

void tep::energy_reader_pcm::print(std::ostream& os) const
{
    os << "# results\n";
    // there are always at least 2 samples
    assert(_samples.size() > 1);
    constexpr const double unable_to_read = -1;
    for (size_t ix = 1; ix < _samples.size(); ix++)
    {
        std::ostringstream buffer;
        uint64_t cnt = _samples[ix - 1].number;
        double cpu = _pcm->packageEnergyMetricsAvailable() ?
            pcm::getConsumedJoules(
                _samples[ix - 1].system_state,
                _samples[ix].system_state)
            : unable_to_read;
        double dram = _pcm->dramEnergyMetricsAvailable() ?
            pcm::getDRAMConsumedJoules(
                _samples[ix - 1].system_state,
                _samples[ix].system_state)
            : unable_to_read;
        double total = cpu + dram;
        buffer << cnt << ',' << total << ',' << cpu << ',' << dram;
        for (pcm::uint32 skt = 0; skt < _pcm->getNumSockets(); skt++)
        {
            cpu = _pcm->packageEnergyMetricsAvailable() ?
                pcm::getConsumedJoules(
                    _samples[ix - 1].socket_states[skt],
                    _samples[ix].socket_states[skt])
                : unable_to_read;
            dram = _pcm->dramEnergyMetricsAvailable() ?
                pcm::getDRAMConsumedJoules(
                    _samples[ix - 1].socket_states[skt],
                    _samples[ix].socket_states[skt])
                : unable_to_read;
            buffer << ',' << cpu << ',' << dram;
        }
        buffer << '\n';
        os << buffer.rdbuf();
    }
}
