// energy_reader_gpu.h
#pragma once

#include "energy_reader_papi.h"

namespace tep
{

class energy_reader_gpu : public energy_reader_papi
{
private:
    struct event_data
    {
        enum class type
        {
            avg_power,
            none
        };
        type type;
        uint32_t device;
        double multiplier;

        event_data(const std::string_view& name);
    };

private:
    std::vector<event_data> _events;

public:
    energy_reader_gpu(size_t init_sample_count);
    energy_reader_gpu(energy_reader_gpu&& other);

    // disable copying
    energy_reader_gpu(const energy_reader_gpu& other) = delete;
    energy_reader_gpu& operator=(const energy_reader_gpu& other) = delete;

    virtual void start() override;
    virtual void sample() override;
    virtual void stop() override;

protected:
    virtual void print(std::ostream& os) const override;

private:
    void add_events(int cid);
};

}
