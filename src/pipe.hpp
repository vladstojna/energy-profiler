// pipe.hpp

#pragma once

#include <array>
#include <iosfwd>
#include <string>
#include <vector>
#include <type_traits>

#include <sys/stat.h>

namespace cmmn
{
    template<typename R, typename E>
    class expected;
};

namespace tep
{

    // error handling

    enum class pipe_error_code
    {
        SUCCESS,
        SYS_ERROR,
        CMD_ERROR,
        UNKNOWN
    };

    class pipe_error
    {
    public:
        static pipe_error success();

    private:
        pipe_error_code _code;
        std::string _msg;

    public:
        pipe_error(pipe_error_code code);
        pipe_error(pipe_error_code code, const char* msg);
        pipe_error(pipe_error_code code, const std::string& msg);
        pipe_error(pipe_error_code code, std::string&& msg);

        pipe_error_code code() const;
        std::string& msg();
        const std::string& msg() const;

        explicit operator bool() const;
    };


    // classes

    class fd_mode
    {
    public:
        static fd_mode rdwr_all;

    private:
        mode_t _mode;

    public:
        fd_mode(mode_t mode);
        mode_t get() const;
    };

    class fd_flags
    {
    public:
        static fd_flags read;
        static fd_flags write;
        static fd_flags swrite;
        static fd_flags append;
        static fd_flags sappend;

    private:
        int _flags;

    public:
        fd_flags(int flags);
        int get() const;
    };

    class file_descriptor
    {
    public:
        static file_descriptor std_in;
        static file_descriptor std_out;
        static file_descriptor std_err;

        static cmmn::expected<file_descriptor, pipe_error> create(const char* path,
            const fd_flags& flags,
            const fd_mode& mode);

    private:
        int _fd;

        bool is_stdfd() const;

        file_descriptor(const char* path, const fd_flags& flags,
            const fd_mode& mode, pipe_error& err);

        pipe_error write(const char* buffer, size_t sz);

    public:
        file_descriptor(int fd);
        ~file_descriptor();

        file_descriptor(file_descriptor&& other);
        file_descriptor(const file_descriptor& other);

        file_descriptor& operator=(file_descriptor&& other);
        file_descriptor& operator=(const file_descriptor& other);

        pipe_error redirect(file_descriptor& newfd);
        pipe_error close();
        pipe_error flush();

        friend file_descriptor& operator<<(file_descriptor& fd, const char* str);
        friend file_descriptor& operator<<(file_descriptor& fd, const std::string& str);
    };

    class pipe
    {
    public:
        static cmmn::expected<pipe, pipe_error> create();

    private:
        std::array<file_descriptor, 2> _pipe;

        pipe(pipe_error& err);

    public:
        file_descriptor& read_end();
        const file_descriptor& read_end() const;
        file_descriptor& write_end();
        const file_descriptor& write_end() const;

    };


    class command
    {
    private:
        std::vector<std::string> _args;

    public:
        template<typename U, typename... T>
        command(std::in_place_t, U&& first, T&&... args) :
            _args{ std::forward<U>(first), std::forward<T>(args)... }
        {}

        command(const std::vector<std::string>& args);
        command(std::vector<std::string>&& args);

        const std::vector<std::string>& values() const;
        std::vector<std::string>& values();

        const std::string& path() const;
        std::vector<const char*> argv() const;

    };

    class piped_commands
    {
    private:
        std::vector<command> _cmds;

        template<typename Fin, std::enable_if_t<
            std::is_same_v<std::decay_t<Fin>, file_descriptor>, bool> = true>
            pipe_error execute_impl(Fin&& in, file_descriptor& out) const;

    public:
        template<typename... T>
        piped_commands(T&&... args) :
            _cmds()
        {
            _cmds.emplace_back(std::in_place, std::forward<T>(args)...);
        }

        piped_commands(const command& cmd);
        piped_commands(command&& cmd);

        std::vector<command>& cmds();
        const std::vector<command>& cmds() const;

        template<typename... T>
        piped_commands& add(T&&... args)
        {
            _cmds.emplace_back(std::in_place, std::forward<T>(args)...);
            return *this;
        }

        piped_commands& add(const command& command);
        piped_commands& add(command&& command);

        pipe_error execute(const file_descriptor& in, file_descriptor& out) const;
        pipe_error execute(file_descriptor&& in, file_descriptor& out) const;
    };


    // operator overloads

    std::ostream& operator<<(std::ostream& os, const pipe_error& e);
    std::ostream& operator<<(std::ostream& os, const command& c);
    std::ostream& operator<<(std::ostream& os, const piped_commands& c);

    file_descriptor& operator<<(file_descriptor& fd, const char* str);
    file_descriptor& operator<<(file_descriptor& fd, const std::string& str);
};
