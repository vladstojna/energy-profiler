// reader_rapl.hpp

#pragma once

#include <nrg/constants.hpp>
#include <nrg/reader.hpp>
#include <nrg/types.hpp>

#include <array>
#include <vector>
#include <type_traits>

namespace nrgprf
{

    class error;
    class sample;

    namespace detail
    {
        struct file_descriptor
        {
            static result<file_descriptor> create(const char* file);

            int value;

            file_descriptor(const char* file, error& err);
            ~file_descriptor() noexcept;

            file_descriptor(const file_descriptor& fd) noexcept;
            file_descriptor& operator=(const file_descriptor& other) noexcept;

            file_descriptor(file_descriptor&& fd) noexcept;
            file_descriptor& operator=(file_descriptor&& other) noexcept;
        };

        struct event_data
        {
            file_descriptor fd;
            mutable uint64_t max;
            mutable uint64_t prev;
            mutable uint64_t curr_max;
            event_data(const file_descriptor& fd, uint64_t max);
            event_data(file_descriptor&& fd, uint64_t max);
        };
    }

    class reader_rapl : public reader
    {
    public:
        struct package : std::integral_constant<int32_t, 0> {};
        struct cores : std::integral_constant<int32_t, 1> {};
        struct uncore : std::integral_constant<int32_t, 2> {};
        struct dram : std::integral_constant<int32_t, 3> {};

        struct skt_energy
        {
            uint32_t skt;
            units_energy energy;
        };

    private:
        std::array<std::array<int32_t, rapl_domains>, max_sockets> _event_map;
        std::vector<detail::event_data> _active_events;

    public:
        reader_rapl(rapl_mask, socket_mask, error&);
        reader_rapl(rapl_mask, error&);
        reader_rapl(socket_mask, error&);
        reader_rapl(error&);

        error read(sample& s) const override;
        error read(sample& s, uint8_t ev_idx) const override;
        size_t num_events() const override;

        template<typename Tag>
        int32_t event_idx(uint8_t skt) const;

        template<typename Tag>
        result<units_energy> get_energy(const sample& s, uint8_t skt) const;

        template<typename Tag>
        std::vector<skt_energy> get_energy(const sample& s) const;

    private:
        error add_event(const char* base, rapl_mask dmask, uint8_t skt);
    };

}
