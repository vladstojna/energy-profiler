// trap.hpp

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <set>
#include <filesystem>

#include "config.hpp"

namespace nrgprf
{
    class reader_rapl;
    class reader_gpu;
};

namespace tep
{

    class async_sampler;


    class address
    {
    public:
        using value_type = uintptr_t;

    private:
        value_type _value;

    public:
        address(value_type val);
        value_type val() const;
    };


    class start_addr : public address
    {
    public:
        start_addr(value_type val);
    };


    class end_addr : public address
    {
    public:
        end_addr(value_type val);
    };


    class sampler_creator
    {
    public:
        virtual ~sampler_creator() = default;
        virtual std::unique_ptr<async_sampler> create() const = 0;
    };


    template<typename R>
    class unbounded_creator : public sampler_creator
    {
    private:
        const R* _reader;
        std::chrono::milliseconds _period;
        size_t _initsz;

    public:
        unbounded_creator(const R* reader, const std::chrono::milliseconds& period, size_t initial_size);
        std::unique_ptr<async_sampler> create() const override;
    };


    template<typename R>
    class bounded_creator : public sampler_creator
    {
    private:
        const R* _reader;
        std::chrono::milliseconds _period;

    public:
        bounded_creator(const R* reader, const std::chrono::milliseconds& period);
        std::unique_ptr<async_sampler> create() const override;
    };


    using unbounded_rapl_smpcrt = unbounded_creator<nrgprf::reader_rapl>;
    using unbounded_gpu_smpcrt = unbounded_creator<nrgprf::reader_gpu>;
    using bounded_rapl_smpcrt = bounded_creator<nrgprf::reader_rapl>;
    using bounded_gpu_smpcrt = bounded_creator<nrgprf::reader_gpu>;


    class trap_data
    {
    private:
        uintptr_t _addr;
        long _origw;
        std::unique_ptr<sampler_creator> _creator;

    public:
        trap_data(uintptr_t addr, long ow, std::unique_ptr<sampler_creator>&&);

        uintptr_t address() const;
        long original_word() const;
        std::unique_ptr<async_sampler> create_sampler() const;
    };

    bool operator<(const trap_data& lhs, const trap_data& rhs);
    bool operator<(uintptr_t lhs, const trap_data& rhs);
    bool operator<(const trap_data& lhs, uintptr_t rhs);

    using trap_set = std::set<trap_data, std::less<>>;

}
