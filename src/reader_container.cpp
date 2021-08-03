// reader_container.cpp

#include <cassert>
#include <sstream>

#include "error.hpp"
#include "reader_container.hpp"
#include "util.hpp"

using namespace tep;


// begin helper functions

static tracer_error handle_reader_error(const char* comment, const nrgprf::error& e)
{
    log(log_lvl::error, "%s: error during creation: %s", comment, e.msg().c_str());
    return tracer_error(tracer_errcode::READER_ERROR, e.msg());
}

static nrgprf::reader_rapl
create_cpu_reader(const config_data::params& params, tracer_error& err)
{
    nrgprf::error error = nrgprf::error::success();
    nrgprf::reader_rapl reader(
        nrgprf::location_mask(params.domain_mask()),
        nrgprf::socket_mask(params.socket_mask()),
        error);
    if (error)
        err = handle_reader_error(__func__, error);
    else
        log(log_lvl::success, "created %s reader", "RAPL");
    return reader;
}

static nrgprf::reader_gpu
create_gpu_reader(const config_data::params& params, tracer_error& err)
{
    nrgprf::error error = nrgprf::error::success();
    nrgprf::reader_gpu reader(
        nrgprf::device_mask(params.device_mask()),
        error);
    if (error)
        err = handle_reader_error(__func__, error);
    else
        log(log_lvl::success, "created %s reader", "GPU");
    return reader;
}

// end helper functions

reader_container::reader_container(const config_data& cd, tracer_error& err) :
    _rdr_cpu(create_cpu_reader(cd.parameters(), err)),
    _rdr_gpu(create_gpu_reader(cd.parameters(), err))
{
    // iterate all sections and insert hybrid readers
    for (auto sptr : cd.flat_sections())
    {
        const config_data::section& sec = *sptr;
        assert(!sec.targets().empty());
        if (sec.targets().size() < 2)
            continue;
        emplace_hybrid_reader<decltype(sec.targets()), true>(sec.targets());
    }
}

reader_container::~reader_container() = default;

reader_container::reader_container(const reader_container & other) :
    _rdr_cpu(other._rdr_cpu),
    _rdr_gpu(other._rdr_gpu),
    _hybrids()
{
    _hybrids.reserve(other._hybrids.size());
    for (const auto& [tgts, hr] : other._hybrids)
        emplace_hybrid_reader(tgts);
}

reader_container& reader_container::operator=(const reader_container & other)
{
    _rdr_cpu = other._rdr_cpu;
    _rdr_gpu = other._rdr_gpu;
    _hybrids.clear();
    _hybrids.reserve(other._hybrids.size());
    for (const auto& [tgts, hr] : other._hybrids)
        emplace_hybrid_reader(tgts);
    return *this;

}

reader_container::reader_container(reader_container && other) :
    _rdr_cpu(std::move(other._rdr_cpu)),
    _rdr_gpu(std::move(other._rdr_gpu)),
    _hybrids()
{
    _hybrids.reserve(other._hybrids.size());
    for (auto& [tgts, hr] : other._hybrids)
        emplace_hybrid_reader(std::move(tgts));
}

reader_container& reader_container::operator=(reader_container && other)
{
    _rdr_cpu = std::move(other._rdr_cpu);
    _rdr_gpu = std::move(other._rdr_gpu);
    _hybrids.clear();
    _hybrids.reserve(other._hybrids.size());
    for (auto& [tgts, hr] : other._hybrids)
        emplace_hybrid_reader(std::move(tgts));
    return *this;
}

nrgprf::reader_rapl& reader_container::reader_rapl()
{
    return _rdr_cpu;
}

const nrgprf::reader_rapl& reader_container::reader_rapl() const
{
    return _rdr_cpu;
}

nrgprf::reader_gpu& reader_container::reader_gpu()
{
    return _rdr_gpu;
}

const nrgprf::reader_gpu& reader_container::reader_gpu() const
{
    return _rdr_gpu;
}

const nrgprf::reader*
reader_container::find(const config_data::section::target_cont & targets) const
{
    assert(!targets.empty());
    if (targets.size() == 1)
        return find(*targets.begin());

    for (auto& [tgts, rdr] : _hybrids)
        if (tgts == targets)
        {
            std::stringstream ss;
            ss << tgts;
            log(log_lvl::debug, "retrieved hybrid reader for targets: %s", ss.str().c_str());
            return &rdr;
        }
    assert(false);
    return nullptr;
}

const nrgprf::reader* reader_container::find(config_data::target target) const
{
    switch (target)
    {
    case config_data::target::cpu:
        log(log_lvl::debug, "retrieved %s reader", "RAPL");
        return &_rdr_cpu;
    case config_data::target::gpu:
        log(log_lvl::debug, "retrieved %s reader", "GPU");
        return &_rdr_gpu;
    default:
        assert(false);
        return nullptr;
    }
}

template<typename T, bool Log>
void reader_container::emplace_hybrid_reader(T && targets)
{
    auto& [tgts, hr] = _hybrids.emplace_back(
        std::forward<T>(targets),
        nrgprf::hybrid_reader{});
    for (auto target : tgts)
    {
        switch (target)
        {
        case config_data::target::cpu:
            if constexpr (Log)
                log(log_lvl::debug, "insert %s reader to hybrid", "RAPL");
            hr.push_back(_rdr_cpu);
            break;
        case config_data::target::gpu:
            if constexpr (Log)
                log(log_lvl::debug, "insert %s reader to hybrid", "GPU");
            hr.push_back(_rdr_gpu);
            break;
        default:
            assert(false);
        }
    }
}
