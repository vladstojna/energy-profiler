// reader_rapl.hpp

#pragma once

#include "error.hpp"
#include "rapl_domains.hpp"
#include "sample.hpp"
#include "result.hpp"

#include <array>
#include <vector>

namespace nrgprf
{

    namespace detail
    {
        struct file_descriptor
        {
            static result<file_descriptor> create(const char* file);

            int value;

            file_descriptor(const char* file, error& err);
            ~file_descriptor() noexcept;

            file_descriptor(const file_descriptor& fd) = delete;
            file_descriptor& operator=(const file_descriptor& other) = delete;

            file_descriptor(file_descriptor&& fd) noexcept;
            file_descriptor& operator=(file_descriptor&& other) noexcept;
        };

        struct event_data
        {
            file_descriptor fd;
            uint64_t prev;
            uint64_t max;
            event_data(file_descriptor&& fd, uint64_t p, uint64_t m);
        };
    }

    class reader_rapl
    {
    private:
        int8_t _event_map[MAX_SOCKETS][MAX_RAPL_DOMAINS];
        std::vector<detail::event_data> _active_events;

    public:
        reader_rapl(error& ec);
        reader_rapl(rapl_domain dmask, uint8_t skt_mask, error& ec);

        error read(sample& s);
        error read(sample& s, uint8_t ev_idx);

        int8_t event_idx(rapl_domain domain, uint8_t skt) const;
        size_t num_events() const;

        result<uint64_t> get_pkg_energy(const sample& s, uint8_t skt) const;
        result<uint64_t> get_pp0_energy(const sample& s, uint8_t skt) const;
        result<uint64_t> get_pp1_energy(const sample& s, uint8_t skt) const;
        result<uint64_t> get_dram_energy(const sample& s, uint8_t skt) const;

    private:
        error add_event(const char* base, rapl_domain dmask, uint8_t skt);
    };

}
