// reader_container.cpp

#include "config.hpp"
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

static nrgprf::reader_rapl create_cpu_reader(const config_data& cd, tracer_error& err)
{
    nrgprf::error error = nrgprf::error::success();
    nrgprf::reader_rapl reader(
        nrgprf::rapl_mask(cd.parameters().domain_mask()),
        nrgprf::socket_mask(cd.parameters().socket_mask()),
        error);
    if (error)
        err = handle_reader_error(__func__, error);
    else
        log(log_lvl::success, "created %s reader", "RAPL");
    return reader;
}

static nrgprf::reader_gpu create_gpu_reader(const config_data& cd, tracer_error& err)
{
    nrgprf::error error = nrgprf::error::success();
    nrgprf::reader_gpu reader(
        nrgprf::device_mask(cd.parameters().device_mask()),
        error);
    if (error)
        err = handle_reader_error(__func__, error);
    else
        log(log_lvl::success, "created %s reader", "GPU");
    return reader;
}

// end helper functions

reader_container::reader_container(const config_data& cd, tracer_error& err) :
    _rdr_cpu(create_cpu_reader(cd, err)),
    _rdr_gpu(create_gpu_reader(cd, err))
{}

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
