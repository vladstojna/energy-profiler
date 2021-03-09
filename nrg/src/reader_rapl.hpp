// reader_rapl.hpp

#pragma once

#include "error.hpp"
#include "event_cpu.hpp"
#include "rapl_domains.hpp"
#include "sample.hpp"

#include <array>

namespace nrgprf
{

    class reader_rapl
    {
    private:
        int _evset;
        rapl_domain _dmask;
        uint8_t _skt_mask;
        std::array<event_cpu, MAX_SOCKETS> _events;

    public:
        reader_rapl(rapl_domain dmask, uint8_t skt_mask, error& ec);
        ~reader_rapl() noexcept;

        // forbid copies
        reader_rapl(const reader_rapl& other) = delete;
        reader_rapl& operator=(const reader_rapl& other) = delete;

        const event_cpu& event(size_t skt) const;
        error read(sample& s) const;

    private:
        error add_events(int cid);
    };

}
