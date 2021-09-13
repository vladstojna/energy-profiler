#pragma once

#include <iosfwd>
#include <string>
#include <mutex>

namespace tep
{
    class log
    {
    private:
        log() = default;

    public:
        enum level
        {
            debug,
            info,
            success,
            warning,
            error
        };

        struct loc
        {
            const char* file;
            int line;
            explicit operator bool() const;
        };

        struct content
        {
            std::string msg;

            template<typename... Args>
            content(const char* fmt, const Args&... args)
            {
                init(fmt, args...);
            }

            explicit operator bool() const;

        private:
            void init(const char*, ...);
        };

        static void init(bool quiet = false, const std::string& path = "");

        static std::mutex& mutex();

        static std::ostream& stream(level lvl = info);

        static std::ostream& flush(level lvl = info);

        static void write(level lvl, const content& cnt, loc at);

    #define logline(lvl, ...) \
        write((lvl), { __VA_ARGS__ }, { __FILE__, __LINE__ })
    };
}
