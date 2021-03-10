// event_cpu.hpp

#pragma once

#include "rapl_domains.hpp"
#include "result.hpp"
#include "sample.hpp"

namespace nrgprf
{

    class event_cpu
    {
    private:
        rapl_domain _eff_dmask;
        uint8_t _pkg, _pp0, _pp1, _dram;
        double _mult;

    public:
        event_cpu();
        event_cpu(rapl_domain effd,
            uint8_t pkg,
            uint8_t pp0,
            uint8_t pp1,
            uint8_t dram,
            double mult);

        result<long long> get_pkg(const sample& s) const;
        result<long long> get_pp0(const sample& s) const;
        result<long long> get_pp1(const sample& s) const;
        result<long long> get_dram(const sample& s) const;

        friend std::ostream& operator<<(std::ostream& os, const event_cpu& e);
    };

    std::ostream& operator<<(std::ostream& os, const event_cpu& e);

}

