// energy_reader_pcm.h
#pragma once

#include "energy_reader.h"

#include <vector>
#include <iosfwd>

#include <cpucounters.h>

namespace tep
{

class energy_reader_pcm : public energy_reader
{
private:
    struct sample_point
    {
        uint64_t number;
        pcm::SystemCounterState system_state;
        std::vector<pcm::SocketCounterState> socket_states;
        std::vector<pcm::CoreCounterState>& core_dummy_states;

        sample_point(uint64_t count,
            uint32_t num_skts,
            std::vector<pcm::CoreCounterState>& ccs);
    };

private:
    std::vector<sample_point> _samples;
    // these dummy states only exist to have a successful PCM call
    std::vector<pcm::CoreCounterState> _dummy_states;
    pcm::PCM* _pcm;

public:
    energy_reader_pcm();
    energy_reader_pcm(energy_reader_pcm&& other);
    // disable copying
    energy_reader_pcm(const energy_reader_pcm& other) = delete;
    energy_reader_pcm& operator=(const energy_reader_pcm& other) = delete;

    virtual void start() override {}
    virtual void sample() override;
    virtual void stop() override {}

protected:
    virtual void print(std::ostream& os) const override;
};

}
