#include "log.hpp"

#include <cinttypes>
#include <cstring>
#include <cstdarg>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <array>
#include <sstream>

using namespace tep;

#define COLOR_RED "\x1B[31m"
#define COLOR_GREEN "\x1B[32m"
#define COLOR_YELLOW "\x1B[33m"
#define COLOR_BLUE "\x1B[34m"
#define COLOR_CYAN "\x1B[36m"
#define COLOR_RESET "\x1B[0m"

namespace
{
    using write_func_ptr = void(*)(const log::content&, log::loc);
    using stream_getter_ptr = std::ostream& (*)();

    constexpr const char error_message[] = "<log error>";

    constexpr const std::array<const char*, 5> levels =
    {
        "debug",
        "info",
        "success",
        "warn",
        "error"
    };

    constexpr const std::array<const char*, 5> clevels =
    {
        COLOR_BLUE "debug" COLOR_RESET,
        COLOR_CYAN "info" COLOR_RESET,
        COLOR_GREEN "success" COLOR_RESET,
        COLOR_YELLOW "warn" COLOR_RESET,
        COLOR_RED "error" COLOR_RESET
    };

    template<bool colored = false>
    constexpr const char* to_string(log::level lvl)
    {
        if constexpr (colored)
            return clevels[lvl];
        else
            return levels[lvl];
    }

    std::mutex _logmtx;
    std::ofstream _stream;
    stream_getter_ptr stream_getter = nullptr;

    class timestamp
    {
        char buff[128];

    public:
        timestamp()
        {
            using namespace std::chrono;
            system_clock::time_point stp(system_clock::now());
            microseconds us = duration_cast<microseconds>(stp.time_since_epoch());
            seconds sec = duration_cast<seconds>(us);
            std::tm tm;
            std::time_t time = sec.count();
            localtime_r(&time, &tm);
            size_t tm_sz = std::strftime(buff, sizeof(buff), "%T", &tm);
            if (!tm_sz)
            {
                *buff = 0;
                return;
            }
            int written = snprintf(buff + tm_sz, sizeof(buff) - tm_sz, ".%06" PRId64,
                us.count() % microseconds::period::den);
            if (written < 0 || static_cast<unsigned>(written) >= sizeof(buff) - tm_sz)
            {
                *buff = 0;
                return;
            }
        }

        explicit operator bool() const
        {
            return *buff;
        }

        friend std::ostream& operator<<(std::ostream& os, const timestamp& ts);
    };

    std::ostream& operator<<(std::ostream& os, const timestamp& ts)
    {
        return os << ts.buff;
    }

    std::ostream& operator<<(std::ostream& os, tep::log::loc at)
    {
        std::ios::fmtflags flags(os.flags());
        os << at.file << ":" << std::left << std::setw(3) << at.line;
        os.flags(flags);
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const tep::log::content& cnt)
    {
        os << cnt.msg;
        return os;
    }

    std::ostream& operator<<(std::ostream& os, tep::log::level lvl)
    {
        os << to_string(lvl);
        return os;
    }

    void write_single(std::ostream& os, log::level lvl, const log::content& cnt, log::loc at)
    {
        auto ts = timestamp{};
        if (ts && cnt)
            os << timestamp{} << ": " << at << " " << lvl << ": " << cnt << "\n";
        else
            os << error_message << "\n";
    }

    template<typename... Args>
    void write_multiple(log::level lvl, const log::content& cnt, log::loc at, Args&... streams)
    {
        std::ostringstream oss;
        write_single(oss, lvl, cnt, at);

        std::string str = oss.str();
        auto print = [](std::ostream& os, const std::string& str)
        {
            os << str;
        };
        (print(streams, str), ...);
    }

    template<log::level lvl, bool to_file = false, bool duplicate = false>
    void write_impl(const log::content& cnt, log::loc at)
    {
        static_assert(to_file || !duplicate,
            "If not writing to file, cannot duplicate to std* stream");

        if constexpr (lvl == log::error || lvl == log::warning)
        {
            if constexpr (to_file)
            {
                if constexpr (lvl == log::error && duplicate)
                    write_multiple(lvl, cnt, at, _stream, std::cerr);
                else
                    write_single(_stream, lvl, cnt, at);
            }
            else
                write_single(std::cerr, lvl, cnt, at);
        }
        else if constexpr (to_file)
            write_single(_stream, lvl, cnt, at);
        else
            write_single(std::cout, lvl, cnt, at);
    }

    void do_nothing(const log::content&, log::loc) {}

    std::array<write_func_ptr, 5> funcs =
    {
        do_nothing,
        do_nothing,
        do_nothing,
        do_nothing,
        do_nothing
    };

    template<bool to_file, bool dup, log::level... lvl>
    void set_funcs(std::array<write_func_ptr, 5>& funcs)
    {
        static_assert(to_file || !dup,
            "If not writing to file, cannot duplicate to std* stream");

        if constexpr (to_file && dup)
            ((funcs[lvl] = write_impl<lvl, true, true>), ...);
        else if constexpr (to_file)
            ((funcs[lvl] = write_impl<lvl, true>), ...);
        else
            ((funcs[lvl] = write_impl<lvl>), ...);
    }

    std::string error_opening_file(const std::string& file)
    {
        std::string msg("Error opening file ");
        msg.append(file).append(": ").append(std::strerror(errno));
        return msg;
    }

    std::ostream& get_default_stream()
    {
        return std::cout;
    }

    std::ostream& get_fstream()
    {
        return _stream;
    }

    static_assert(funcs.size() == log::error + 1);
    static_assert(levels.size() == log::error + 1);
    static_assert(clevels.size() == log::error + 1);
}

namespace tep
{
    log::loc::operator bool() const
    {
        return file && line;
    }

    log::content::content(const char* fmt, ...) :
        msg()
    {
        va_list args;
        va_list args_copy;
        va_start(args, fmt);
        va_copy(args_copy, args);
        ssize_t bufsz = std::vsnprintf(nullptr, 0, fmt, args);
        va_end(args);
        if (bufsz < 0)
            return;

        msg.resize(bufsz + 1);
        bufsz = std::vsnprintf(msg.data(), msg.capacity(), fmt, args_copy);
        va_end(args_copy);
        if (bufsz < 0)
            msg.resize(0);
    }

    log::content::operator bool() const
    {
        return !msg.empty();
    }

    void log::init(bool quiet, const std::string& path)
    {
        static std::once_flag oflag;
        try
        {
            std::call_once(oflag, [](bool quiet, const std::string& path)
                {
                    if (quiet)
                    {
                        _stream.setstate(std::ios::badbit);
                        stream_getter = get_fstream;
                        set_funcs<false, false, error>(funcs);
                    }
                    else if (path.empty())
                    {
                        stream_getter = get_default_stream;
                        set_funcs<false, false, debug, info, success, warning, error>(funcs);
                    }
                    else
                    {
                        _stream.open(path);
                        if (!_stream)
                            throw std::runtime_error(error_opening_file(path));
                        stream_getter = get_fstream;
                        set_funcs<true, false, debug, info, success, warning>(funcs);
                        set_funcs<true, true, error>(funcs);
                    }
                }, quiet, path);
        }
        catch (...)
        {
            throw;
        }
    }

    std::mutex& log::mutex()
    {
        return _logmtx;
    }

    std::ostream& log::stream()
    {
        return (*stream_getter)();
    }

    void log::write(level lvl, const content& cnt, loc at)
    {
        std::scoped_lock lock(_logmtx);
        (*funcs[lvl])(cnt, at);
    }
}
