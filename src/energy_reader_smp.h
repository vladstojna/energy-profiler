// energy_reader_smp.h
#pragma once

#include "energy_reader_papi.h"

namespace tep
{

class energy_reader_smp : public energy_reader_papi
{
private:
    struct event_data
    {
        enum class type
        {
            pkg_energy,
            dram_energy,
            none
        };
        type type;
        uint32_t socket;
        double multiplier;

        event_data(const std::string_view& name,
            const std::string_view& units);
    };

private:
    std::vector<event_data> _events;

public:
    energy_reader_smp(size_t init_sample_count);
    energy_reader_smp(energy_reader_smp&& other);

    // disable copying
    energy_reader_smp(const energy_reader_smp& other) = delete;
    energy_reader_smp& operator=(const energy_reader_smp& other) = delete;

    virtual void start() override;
    virtual void sample() override;
    virtual void stop() override;

protected:
    virtual void print(std::ostream& os) const override;

private:
    void add_events(int cid);
};

}
