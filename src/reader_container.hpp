// reader_container.hpp

#pragma once

#include "config.hpp"
#include "flags.hpp"

#include <nrg/reader_gpu.hpp>
#include <nrg/reader_rapl.hpp>
#include <nrg/hybrid_reader.hpp>

namespace tep
{

    class tracer_error;

    class reader_container
    {
    private:
        nrgprf::reader_rapl _rdr_cpu;
        nrgprf::reader_gpu _rdr_gpu;

        std::vector<std::pair<
            config_data::section::target_cont,
            nrgprf::hybrid_reader>
        > _hybrids;

    public:
        reader_container(const flags& flags, const config_data& cd, tracer_error& err);

        ~reader_container(); // = default;

        reader_container(const reader_container& other);
        reader_container& operator=(const reader_container& other);

        reader_container(reader_container&& other);
        reader_container& operator=(reader_container&& other);

        nrgprf::reader_rapl& reader_rapl();
        const nrgprf::reader_rapl& reader_rapl() const;

        nrgprf::reader_gpu& reader_gpu();
        const nrgprf::reader_gpu& reader_gpu() const;

        const nrgprf::reader* find(const config_data::section::target_cont& targets) const;
        const nrgprf::reader* find(config_data::target) const;

    private:
        template<typename T, bool Log = false>
        void emplace_hybrid_reader(T&& targets);
    };

}