// config.hpp

#pragma once

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

        TASK_LIST_EMPTY,
        TASK_NO_TARGET,
        TASK_NO_SECTION,
        TASK_INVALID_TARGET,
        TASK_INVALID_EXECS,
        TASK_INVALID_NAME,
        TASK_INVALID_EXTRA,
        TASK_INVALID_METHOD,

        SEC_NO_START_NODE,
        SEC_NO_END_NODE,

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
        {
        }

        cfg_error_code code() const
        {
            return _code;
        }

        operator bool() const
        {
            return _code == cfg_error_code::SUCCESS;
        }
    };

    std::ostream& operator<<(std::ostream& os, const cfg_error& res);

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
            {
            }

            position(std::string&& cu, uint32_t ln) :
                compilation_unit(std::move(cu)), line(ln)
            {
            }

            position(const char* cu, uint32_t ln) :
                compilation_unit(cu), line(ln)
            {
            }
        };

        struct section
        {
            config_data::position start;
            config_data::position end;

            template<typename T, std::enable_if_t<std::is_same_v<T, config_data::position>, bool> = true>
            section(T&& s, T&& e) :
                start(std::forward<T>(s)), end(std::forward<T>(e))
            {
            }
        };

        struct task
        {
            std::string name;
            std::string extra;
            config_data::target target;
            config_data::profiling_method method;
            config_data::section section;
            uint32_t executions;

            template<typename T1, typename T2, typename T3, std::enable_if_t<
                std::is_same_v<T1, std::string> || std::is_same_v<T1, const char*> ||
                std::is_same_v<T2, std::string> || std::is_same_v<T2, const char*> ||
                std::is_same_v<T3, config_data::section>, bool> = true
            > task(T1&& nm,
                T2&& extr,
                config_data::target tgt,
                config_data::profiling_method mthd,
                T3&& sec,
                uint32_t execs) :
                name(std::forward<T1>(nm)),
                extra(std::forward<T2>(extr)),
                target(tgt),
                method(mthd),
                section(std::forward<T3>(sec)),
                executions(execs)
            {
            }
        };

        uint32_t threads;
        std::vector<task> tasks;
    };

    // types

    template<typename R>
    using cfg_expected = cmmn::expected<R, cfg_error>;
    using cfg_result = cfg_expected<config_data>;

    // functions

    cfg_result load_config(const char* file);

}
