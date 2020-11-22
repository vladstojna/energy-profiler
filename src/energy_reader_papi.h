// energy_reader_papi.h
#pragma once

#include "energy_reader.h"

#include <vector>

namespace tep
{

class energy_reader_papi : public energy_reader
{
protected:
    struct sample_point : energy_reader::basic_sample
    {
        std::vector<long long> values;
        sample_point(const timepoint_t& tp, size_t num_events);
    };

private:
    int _event_set;
    std::vector<sample_point> _samples;

public:
    energy_reader_papi(size_t init_sample_count);
    energy_reader_papi(energy_reader_papi&& other);
    ~energy_reader_papi();

    // disable copying
    energy_reader_papi(const energy_reader_papi& other) = delete;
    energy_reader_papi& operator=(const energy_reader_papi& other) = delete;

protected:
    static int find_component(const char* cmp_name);

    int event_set() const
    {
        return _event_set;
    }

    std::vector<sample_point>& samples()
    {
        return _samples;
    }

    const std::vector<sample_point>& samples() const
    {
        return _samples;
    }
};

}
