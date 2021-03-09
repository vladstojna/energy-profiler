// nrg.cpp
// implementation of C wrapper to C++ code

extern "C"
{
#include "nrg.h"
}

#include "nrg.hpp"

#include <cstring>

#if !defined(FILELINE)
#define FILELINE 1
#endif

#if FILELINE

#define nrg_str__(x) #x
#define nrg_stringify__(x) nrg_str__(x)
#define nrg_fl(str_literal) __FILE__ "@" nrg_stringify__(__LINE__) ": " str_literal

#else

#define nrg_fl(str_literal) str_literal

#endif

nrg_error_t error_success = { NRG_SUCCESS, "no error" };
nrg_error_t error_bad_alloc = { NRG_EBADALLOC, "could not allocate memory" };
nrg_error_t error_unknown = { NRG_EUNKNOWN, "unknown error" };

nrg_error_t create_error(nrg_error_code_t code, const char* msg)
{
    nrg_error_t error;
    error.code = code;
    strncpy(error.msg, msg, NRG_ERROR_MSG_LEN);
    error.msg[NRG_ERROR_MSG_LEN - 1] = '\0';
    return error;
}


// nrgTask_*


nrg_error_t nrgTask_create(nrg_task_t* task, unsigned int id)
{
    *task = reinterpret_cast<nrg_task_t>(new (std::nothrow) nrgprf::task(id));
    if (*task == nullptr)
        return error_bad_alloc;
    return error_success;
}

void nrgTask_destroy(nrg_task_t task)
{
    delete reinterpret_cast<nrgprf::task*>(task);
}

unsigned int nrgTask_id(const nrg_task_t task)
{
    return reinterpret_cast<nrgprf::task*>(task)->id();
}

unsigned int nrgTask_exec_count(const nrg_task_t task)
{
    return reinterpret_cast<nrgprf::task*>(task)->size();
}

nrg_exec_t nrgTask_exec_by_idx(const nrg_task_t task, unsigned int idx)
{
    return reinterpret_cast<nrg_exec_t>(
        &reinterpret_cast<nrgprf::task*>(task)->get(idx));
}

nrg_error_t nrgTask_add_exec(nrg_task_t task, unsigned int* out_idx)
{
    try
    {
        *out_idx = reinterpret_cast<nrgprf::task*>(task)->add();
        return error_success;
    }
    catch (const std::bad_alloc& e)
    {
        return create_error(NRG_EBADALLOC, nrg_fl("could not allocate memory"));
    }
    catch (const std::exception& e)
    {
        return error_unknown;
    }
}

nrg_error_t nrgTask_add_execs(nrg_task_t task, unsigned int* idxs, unsigned int sz)
{
    try
    {
        for (unsigned int i = 0; i < sz; i++)
            idxs[i] = reinterpret_cast<nrgprf::task*>(task)->add();
        return error_success;
    }
    catch (const std::bad_alloc& e)
    {
        return error_bad_alloc;
    }
    catch (const std::exception& e)
    {
        return error_unknown;
    }
}

nrg_error_t nrgTask_reserve(nrg_task_t task, unsigned int num_execs)
{
    try
    {
        reinterpret_cast<nrgprf::task*>(task)->reserve(num_execs);
        return error_success;
    }
    catch (const std::bad_alloc& e)
    {
        return create_error(NRG_EBADALLOC, nrg_fl("could not allocate memory"));
    }
    catch (const std::exception& e)
    {
        return error_unknown;
    }
}


// nrgExec_*


unsigned int nrgExec_id(const nrg_exec_t exec)
{
    return reinterpret_cast<nrgprf::execution*>(exec)->id();
}

unsigned int nrgExec_sample_count(const nrg_exec_t exec)
{
    return reinterpret_cast<nrgprf::execution*>(exec)->size();
}

nrg_sample_t nrgExec_sample_by_idx(const nrg_exec_t exec, unsigned int idx)
{
    return reinterpret_cast<nrg_sample_t>(
        &reinterpret_cast<nrgprf::execution*>(exec)->get(idx));
}

nrg_sample_t nrgExec_first_sample(const nrg_exec_t exec)
{
    return reinterpret_cast<nrg_sample_t>(
        &reinterpret_cast<nrgprf::execution*>(exec)->first());
}

nrg_sample_t nrgExec_last_sample(const nrg_exec_t exec)
{
    return reinterpret_cast<nrg_sample_t>(
        &reinterpret_cast<nrgprf::execution*>(exec)->last());
}

nrg_error_t nrgExec_reserve(nrg_exec_t exec, unsigned int num_samples)
{
    try
    {
        reinterpret_cast<nrgprf::execution*>(exec)->reserve(num_samples);
        return error_success;
    }
    catch (const std::bad_alloc& e)
    {
        return create_error(NRG_EBADALLOC, nrg_fl("could not allocate memory"));
    }
    catch (const std::exception& e)
    {
        return error_unknown;
    }
}

nrg_error_t nrgExec_add_sample(nrg_exec_t exec, long long clk_ticks, unsigned int* outidx)
{
    try
    {
        *outidx = reinterpret_cast<nrgprf::execution*>(exec)->add(
            nrgprf::timepoint_t(std::chrono::nanoseconds(clk_ticks)));
        return error_success;
    }
    catch (const std::bad_alloc& e)
    {
        return create_error(NRG_EBADALLOC, nrg_fl("could not allocate memory"));
    }
    catch (const std::exception& e)
    {
        return error_unknown;
    }
}


// nrgSample_*


long long nrgSample_timepoint(const nrg_sample_t sample)
{
    return reinterpret_cast<nrgprf::sample*>(sample)->timepoint().time_since_epoch().count();
}

long long nrgSample_duration(const nrg_sample_t lhs, const nrg_sample_t rhs)
{
    return (*reinterpret_cast<nrgprf::sample*>(lhs) - *reinterpret_cast<nrgprf::sample*>(rhs))
        .count();
}

void nrgSample_update(nrg_sample_t sample, long long clk_ticks)
{
    reinterpret_cast<nrgprf::sample*>(sample)->timepoint(
        nrgprf::timepoint_t(std::chrono::nanoseconds(clk_ticks)));
}


// nrgReader_*


nrg_error_t nrgReader_create_cpu(nrg_reader_cpu_t* outrdr, unsigned char skt_mask, unsigned char domains)
{
    try
    {
        nrgprf::error error = nrgprf::error::success();
        *outrdr = reinterpret_cast<nrg_reader_cpu_t>(
            new (std::nothrow) nrgprf::reader_rapl(
                static_cast<nrgprf::rapl_domain>(domains), skt_mask, error));
        if (*outrdr == nullptr)
            return error_bad_alloc;
        if (error)
        {
            nrgReader_destroy_cpu(*outrdr);
            return create_error(
                static_cast<nrg_error_code_t>(error.code()),
                error.msg().c_str());
        }
        return error_success;
    }
    catch (const std::exception& e)
    {
        return create_error(NRG_ESETUP, e.what());
    }
}

nrg_error_t nrgReader_create_gpu(nrg_reader_gpu_t* outrdr, unsigned char dev_mask)
{
    try
    {
        nrgprf::error error = nrgprf::error::success();
        *outrdr = reinterpret_cast<nrg_reader_gpu_t>(
            new (std::nothrow) nrgprf::reader_gpu(dev_mask, error));
        if (*outrdr == nullptr)
            return error_bad_alloc;
        if (error)
        {
            nrgReader_destroy_gpu(*outrdr);
            return create_error(
                static_cast<nrg_error_code_t>(error.code()),
                error.msg().c_str());
        }
        return error_success;
    }
    catch (const std::exception& e)
    {
        return create_error(NRG_ESETUP, e.what());
    }
}

void nrgReader_destroy_cpu(nrg_reader_cpu_t reader)
{
    delete reinterpret_cast<nrgprf::reader_rapl*>(reader);
}

void nrgReader_destroy_gpu(nrg_reader_gpu_t reader)
{
    delete reinterpret_cast<nrgprf::reader_gpu*>(reader);
}

nrg_error_t nrgReader_read_cpu(const nrg_reader_cpu_t reader, nrg_sample_t sample)
{
    nrgprf::error err = reinterpret_cast<nrgprf::reader_rapl*>(reader)->read(
        *reinterpret_cast<nrgprf::sample*>(sample));
    if (err)
        return create_error(
            static_cast<nrg_error_code_t>(err.code()), err.msg().c_str());
    return error_success;
}

nrg_error_t nrgReader_read_gpu(const nrg_reader_gpu_t reader, nrg_sample_t sample)
{
    nrgprf::error err = reinterpret_cast<nrgprf::reader_gpu*>(reader)->read(
        *reinterpret_cast<nrgprf::sample*>(sample));
    if (err)
        return create_error(
            static_cast<nrg_error_code_t>(err.code()), err.msg().c_str());
    return error_success;
}

nrg_event_cpu_t nrgReader_event_cpu(const nrg_reader_cpu_t reader, unsigned char skt)
{
    return reinterpret_cast<nrg_event_cpu_t>(
        &const_cast<nrgprf::event_cpu&>(
            reinterpret_cast<nrgprf::reader_rapl*>(reader)->event(skt)));
}

nrg_event_gpu_t nrgReader_event_gpu(const nrg_reader_gpu_t reader, unsigned char dev)
{
    return reinterpret_cast<nrg_event_gpu_t>(
        &const_cast<nrgprf::event_gpu&>(
            reinterpret_cast<nrgprf::reader_gpu*>(reader)->event(dev)));
}

// nrgEvent_*

nrg_error_t nrgEvent_pkg(const nrg_event_cpu_t event, const nrg_sample_t sample, long long* outval)
{
    nrgprf::result<long long> result = reinterpret_cast<nrgprf::event_cpu*>(event)->get_pkg(
        *reinterpret_cast<nrgprf::sample*>(sample));
    if (!result)
        return create_error(
            static_cast<nrg_error_code_t>(result.error().code()),
            result.error().msg().c_str());
    *outval = result.value();
    return error_success;
}

nrg_error_t nrgEvent_pp0(const nrg_event_cpu_t event, const nrg_sample_t sample, long long* outval)
{
    nrgprf::result<long long> result = reinterpret_cast<nrgprf::event_cpu*>(event)->get_pp0(
        *reinterpret_cast<nrgprf::sample*>(sample));
    if (!result)
        return create_error(
            static_cast<nrg_error_code_t>(result.error().code()),
            result.error().msg().c_str());
    *outval = result.value();
    return error_success;
}

nrg_error_t nrgEvent_pp1(const nrg_event_cpu_t event, const nrg_sample_t sample, long long* outval)
{
    nrgprf::result<long long> result = reinterpret_cast<nrgprf::event_cpu*>(event)->get_pp1(
        *reinterpret_cast<nrgprf::sample*>(sample));
    if (!result)
        return create_error(
            static_cast<nrg_error_code_t>(result.error().code()),
            result.error().msg().c_str());
    *outval = result.value();
    return error_success;
}

nrg_error_t nrgEvent_dram(const nrg_event_cpu_t event, const nrg_sample_t sample, long long* outval)
{
    nrgprf::result<long long> result = reinterpret_cast<nrgprf::event_cpu*>(event)->get_dram(
        *reinterpret_cast<nrgprf::sample*>(sample));
    if (!result)
        return create_error(
            static_cast<nrg_error_code_t>(result.error().code()),
            result.error().msg().c_str());
    *outval = result.value();
    return error_success;
}

nrg_error_t nrgEvent_board_pwr(const nrg_event_gpu_t event, const nrg_sample_t sample, long long* outval)
{
    nrgprf::result<long long> result = reinterpret_cast<nrgprf::event_gpu*>(event)->get_board_pwr(
        *reinterpret_cast<nrgprf::sample*>(sample));
    if (!result)
        return create_error(
            static_cast<nrg_error_code_t>(result.error().code()),
            result.error().msg().c_str());
    *outval = result.value();
    return error_success;
}


// other


int nrg_is_error(const nrg_error_t* err)
{
    return (err->code != NRG_SUCCESS);
}
