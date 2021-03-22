// config.hpp

#pragma once

#include <chrono>
#include <iosfwd>
#include <string>
#include <vector>

#include <expected.hpp>

namespace tep
{

    // error handling

    enum class cfg_error_code
    {
        SUCCESS = 0,

        CONFIG_IO_ERROR,
        CONFIG_NOT_FOUND,
        CONFIG_OUT_OF_MEM,
        CONFIG_BAD_FORMAT,
        CONFIG_NO_CONFIG,

        INVALID_THREAD_CNT,

        SEC_LIST_EMPTY,
        SEC_NO_BOUNDS,
        SEC_NO_FREQ,
        SEC_INVALID_TARGET,
        SEC_INVALID_NAME,
        SEC_INVALID_EXTRA,
        SEC_INVALID_FREQ,
        SEC_INVALID_INTERVAL,
        SEC_INVALID_METHOD,
        SEC_INVALID_EXECS,
        SEC_INVALID_SAMPLES,
        SEC_INVALID_DURATION,

        PARAM_INVALID_DOMAIN_MASK,
        PARAM_INVALID_SOCKET_MASK,
        PARAM_INVALID_DEVICE_MASK,

        BOUNDS_NO_START,
        BOUNDS_NO_END,

        POS_NO_COMP_UNIT,
        POS_NO_LINE,
        POS_INVALID_COMP_UNIT,
        POS_INVALID_LINE
    };

    class cfg_error
    {
    private:
        cfg_error_code _code;

    public:
        cfg_error(cfg_error_code code) :
            _code(code)
        {}

        cfg_error_code code() const
        {
            return _code;
        }

        operator bool() const
        {
            return _code != cfg_error_code::SUCCESS;
        }
    };

    // structs

    struct config_data
    {
        enum class profiling_method
        {
            energy_profile,
            energy_total
        };

        enum class target
        {
            cpu,
            gpu
        };

        struct position
        {
            std::string compilation_unit;
            uint32_t line;

            position(const std::string& cu, uint32_t ln) :
                compilation_unit(cu), line(ln)
            {}

            position(std::string&& cu, uint32_t ln) :
                compilation_unit(std::move(cu)), line(ln)
            {}

            position(const char* cu, uint32_t ln) :
                compilation_unit(cu), line(ln)
            {}
        };

        struct bounds
        {
            config_data::position start;
            config_data::position end;

            template<typename T, std::enable_if_t<std::is_same_v<T, config_data::position>, bool> = true>
            bounds(T&& s, T&& e) :
                start(std::forward<T>(s)), end(std::forward<T>(e))
            {}
        };

        struct params
        {
            unsigned int domain_mask;
            unsigned int socket_mask;
            unsigned int device_mask;

            params() : params(~0x0, ~0x0, ~0x0)
            {}

            params(unsigned int dommask, unsigned int sktmask, unsigned int devmask) :
                domain_mask(dommask),
                socket_mask(sktmask),
                device_mask(devmask)
            {}
        };

        struct section
        {
            std::string name;
            std::string extra;
            config_data::target target;
            std::chrono::milliseconds interval;
            config_data::profiling_method method;
            config_data::bounds bounds;
            uint32_t executions;
            uint32_t samples;

            template<typename T1, typename T2, typename T3, std::enable_if_t<
                std::is_same_v<T1, std::string> || std::is_same_v<T1, const char*> ||
                std::is_same_v<T2, std::string> || std::is_same_v<T2, const char*> ||
                std::is_same_v<T3, config_data::bounds>, bool> = true
            > section(T1&& nm, T2&& extr,
                config_data::target tgt,
                const std::chrono::milliseconds& intrv,
                config_data::profiling_method mthd,
                T3&& bnd,
                uint32_t execs,
                uint32_t smp) :
                name(std::forward<T1>(nm)),
                extra(std::forward<T2>(extr)),
                target(tgt),
                interval(intrv),
                method(mthd),
                bounds(std::forward<T3>(bnd)),
                executions(execs),
                samples(smp)
            {}
        };

        uint32_t threads;
        config_data::params parameters;
        std::vector<section> sections;
    };

    // operator overloads

    std::ostream& operator<<(std::ostream& os, const cfg_error& res);
    std::ostream& operator<<(std::ostream& os, const config_data::target& tgt);
    std::ostream& operator<<(std::ostream& os, const config_data::profiling_method& pm);
    std::ostream& operator<<(std::ostream& os, const config_data::params& p);
    std::ostream& operator<<(std::ostream& os, const config_data::position& p);
    std::ostream& operator<<(std::ostream& os, const config_data::bounds& b);
    std::ostream& operator<<(std::ostream& os, const config_data::section& s);
    std::ostream& operator<<(std::ostream& os, const config_data& cd);

    // types

    using cfg_result = cmmn::expected<config_data, cfg_error>;

    // functions

    cfg_result load_config(const char* file);
    cfg_result load_config(const std::string& file);

}
