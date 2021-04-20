// reader_container.hpp

#pragma once

#include <nrg.hpp>

namespace cmmn
{
    template<typename R, typename E>
    class expected;
}

namespace tep
{

    class config_data;

    class tracer_error;

    class reader_container
    {
    private:
        nrgprf::reader_rapl _rdr_cpu;
        nrgprf::reader_gpu _rdr_gpu;

    public:
        reader_container(const config_data& cd, tracer_error& err);

        nrgprf::reader_rapl& reader_rapl();
        const nrgprf::reader_rapl& reader_rapl() const;

        nrgprf::reader_gpu& reader_gpu();
        const nrgprf::reader_gpu& reader_gpu() const;
    };

}