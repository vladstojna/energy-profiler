// pipe.cpp

#include "pipe.hpp"

#include <cstring>
#include <iostream>
#include <sstream>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <unistd.h>
#include <wait.h>

#include <util/expected.hpp>
#include <util/concat.hpp>

#ifdef NDEBUG
#define Assert(x) do { (void)sizeof(x); } while(false)
#else
#include <cassert>
#define Assert(x) assert(x)
#endif

using namespace tep;


static std::string msg_success("No error");
static std::string msg_unknown("Unknown error");

// begin helper functions

static pipe_error get_system_error(const char* prefix)
{
    return pipe_error(pipe_error_code::SYS_ERROR, cmmn::concat(prefix, ": ", strerror(errno)));
}

static pipe_error get_command_error(const char* comment, const command& cmd, int exit_status)
{
    std::stringstream sstream;
    sstream << comment << ": '" << cmd << "'" << " returned with status " << exit_status;
    return pipe_error(pipe_error_code::CMD_ERROR, sstream.str());
}

static pipe_error run_command(const command& cmd, file_descriptor& in, file_descriptor& out)
{
    pipe_error err = in.redirect(file_descriptor::std_in);
    if (err)
        return err;
    err = out.redirect(file_descriptor::std_out);
    if (err)
        return err;
    in.close();
    out.close();
    if (execvp(cmd.path().c_str(), const_cast<char* const*>(cmd.argv().data())) == -1)
        return get_system_error("run_command:execvp()");
    Assert(false);
    return { pipe_error_code::UNKNOWN, "Returned from execvp() without error" };
}

// end helper functions

// begin definition of static variables

fd_mode fd_mode::rdwr_all(S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

fd_flags fd_flags::read(O_RDONLY);
fd_flags fd_flags::write(O_WRONLY | O_CREAT | O_TRUNC);
fd_flags fd_flags::swrite(fd_flags::write.get() | O_SYNC);
fd_flags fd_flags::append(O_WRONLY | O_CREAT | O_APPEND);
fd_flags fd_flags::sappend(fd_flags::append.get() | O_SYNC);

file_descriptor file_descriptor::std_in(STDIN_FILENO);
file_descriptor file_descriptor::std_out(STDOUT_FILENO);
file_descriptor file_descriptor::std_err(STDERR_FILENO);

file_descriptor::endl_t file_descriptor::endl;

// end definition of static variables


pipe_error pipe_error::success()
{
    return { pipe_error_code::SUCCESS };
}

pipe_error::pipe_error(pipe_error_code code) :
    _code(code),
    _msg()
{}

pipe_error::pipe_error(pipe_error_code code, const char* msg) :
    _code(code),
    _msg(msg)
{}

pipe_error::pipe_error(pipe_error_code code, const std::string& msg) :
    _code(code),
    _msg(msg)
{}

pipe_error::pipe_error(pipe_error_code code, std::string&& msg) :
    _code(code),
    _msg(std::move(msg))
{}

pipe_error_code pipe_error::code() const
{
    return _code;
}

std::string& pipe_error::msg()
{
    switch (_code)
    {
    case pipe_error_code::SUCCESS:
        return msg_success;
    case pipe_error_code::UNKNOWN:
        return msg_unknown;
    default:
        return _msg;
    }
}

const std::string& pipe_error::msg() const
{
    switch (_code)
    {
    case pipe_error_code::SUCCESS:
        return msg_success;
    case pipe_error_code::UNKNOWN:
        return msg_unknown;
    default:
        return _msg;
    }
}

pipe_error::operator bool() const
{
    return _code != pipe_error_code::SUCCESS;
}



fd_mode::fd_mode(mode_t mode) :
    _mode(mode)
{}

mode_t fd_mode::get() const
{
    return _mode;
}



fd_flags::fd_flags(int flags) :
    _flags(flags)
{}

int fd_flags::get() const
{
    return _flags;
}



cmmn::expected<file_descriptor, pipe_error> file_descriptor::create(const char* path,
    const fd_flags& flags,
    const fd_mode& mode)
{
    pipe_error err = pipe_error::success();
    file_descriptor fd(path, flags, mode, err);
    if (err)
        return err;
    return fd;
}

file_descriptor::file_descriptor(int fd) :
    _fd(fd)
{}

file_descriptor::file_descriptor(const char* path, const fd_flags& flags,
    const fd_mode& mode, pipe_error& err) :
    _fd(open(path, flags.get(), mode.get()))
{
    if (_fd == -1)
        err = get_system_error("file_descriptor:open()");
}

file_descriptor::~file_descriptor()
{
    close();
}

file_descriptor::file_descriptor(file_descriptor&& other) :
    _fd(std::exchange(other._fd, -1))
{}

file_descriptor::file_descriptor(const file_descriptor& other) :
    _fd(dup(other._fd))
{
    if (_fd == -1)
    {
        perror("file_descriptor:dup()");
        throw std::system_error();
    }
}

file_descriptor& file_descriptor::operator=(file_descriptor&& other)
{
    _fd = std::exchange(other._fd, _fd);
    other.close();
    return *this;
}

file_descriptor& file_descriptor::operator=(const file_descriptor& other)
{
    close();
    _fd = dup(other._fd);
    if (_fd == -1)
    {
        perror("file_descriptor:dup()");
        throw std::system_error();
    }
    return *this;
}

bool file_descriptor::is_stdfd() const
{
    return _fd == STDIN_FILENO || _fd == STDOUT_FILENO || _fd == STDERR_FILENO;
}

void file_descriptor::close()
{
    if (_fd >= 0 && !is_stdfd())
    {
        int retval = ::close(_fd);
        Assert(retval != -1);
        _fd = -1;
    }
}

void file_descriptor::flush()
{
    int retval = fsync(_fd);
    Assert(retval != -1);
}

pipe_error file_descriptor::redirect(file_descriptor& newfd)
{
    if (_fd != newfd._fd)
        if (dup2(_fd, newfd._fd) == -1)
            return get_system_error("file_descriptor:dup2()");
    return pipe_error::success();
}

pipe_error file_descriptor::write(const char* buffer, size_t sz)
{
    for (size_t to_write = sz, written = 0; to_write > 0; to_write -= written)
    {
        ssize_t ret = ::write(_fd, buffer + written, to_write);
        if (ret == -1)
            return get_system_error("file_descriptor:write()");
        written += ret;
    }
    return pipe_error::success();
}



cmmn::expected<tep::pipe, pipe_error> pipe::create()
{
    pipe_error err = pipe_error::success();
    pipe p(err);
    if (err)
        return err;
    return p;
}

pipe::pipe(pipe_error& err) :
    _pipe{ create_pipe(err) }
{}

std::array<file_descriptor, 2> pipe::create_pipe(pipe_error& err)
{
    int fd[2];
    if (::pipe(fd) == -1)
        err = get_system_error("create_pipe:pipe()");
    return { fd[0], fd[1] };
}

file_descriptor& pipe::read_end()
{
    return _pipe.front();
}

const file_descriptor& pipe::read_end() const
{
    return _pipe.front();
}

file_descriptor& pipe::write_end()
{
    return _pipe.back();
}

const file_descriptor& pipe::write_end() const
{
    return _pipe.back();
}



command::command(const std::vector<std::string>& args) :
    _args(args)
{}

command::command(std::vector<std::string>&& args) :
    _args(std::move(args))
{}

const std::vector<std::string>& command::values() const
{
    return _args;
}

std::vector<std::string>& command::values()
{
    return _args;
}

const std::string& command::path() const
{
    return _args.front();
}

std::vector<const char*> command::argv() const
{
    std::vector<const char*> retval;
    retval.reserve(_args.size());
    for (const auto& arg : _args)
        retval.push_back(arg.c_str());
    retval.push_back(nullptr);
    return retval;
}



piped_commands::piped_commands(const command& cmd) :
    _cmds()
{
    _cmds.push_back(cmd);
}

piped_commands::piped_commands(command&& cmd) :
    _cmds()
{
    _cmds.push_back(std::move(cmd));
}

std::vector<command>& piped_commands::cmds()
{
    return _cmds;
}

const std::vector<command>& piped_commands::cmds() const
{
    return _cmds;
}

piped_commands& piped_commands::add(const command& command)
{
    _cmds.push_back(command);
    return *this;
}

piped_commands& piped_commands::add(command&& command)
{
    _cmds.push_back(std::move(command));
    return *this;
}

template<typename Fin, std::enable_if_t<std::is_same_v<std::decay_t<Fin>, file_descriptor>, bool>>
pipe_error piped_commands::execute_impl(Fin&& in, file_descriptor& out) const
{
    file_descriptor _in(std::forward<Fin>(in));
    std::vector<std::pair<command, pid_t>> _children;

    for (size_t i = 0; i < _cmds.size() - 1; i++)
    {
        cmmn::expected<pipe, pipe_error> pfd = pipe::create();
        if (!pfd)
            return std::move(pfd.error());

        pid_t childpid = fork();
        if (childpid == 0)
        {
            pipe_error err = run_command(_cmds[i], _in, pfd.value().write_end());
            if (err)
                return err;
        }
        else if (childpid > 0)
        {
            _in = std::move(pfd.value().read_end());
            _children.emplace_back(_cmds[i], childpid);
        }
        else
            return get_system_error("piped_commands:execute:fork()");
    }

    pid_t childpid = fork();
    if (childpid == 0)
    {
        pipe_error err = run_command(_cmds.back(), _in, out);
        if (err)
            return err;
    }
    else if (childpid > 0)
    {
        _children.emplace_back(_cmds.back(), childpid);
        for (const auto& [cmd, pid] : _children)
        {
            int wait_status;
            pid_t waited_child = waitpid(pid, &wait_status, 0);
            if (waited_child == -1)
                return get_system_error("piped_commands:execute:waitpid()");
            if (WEXITSTATUS(wait_status))
                return get_command_error("piped_commands:execute()", cmd, WEXITSTATUS(wait_status));
        }
    }
    else
        return get_system_error("piped_commands:execute:fork()");
    return pipe_error::success();
}

pipe_error piped_commands::execute(const file_descriptor& in, file_descriptor& out) const
{
    return execute_impl(in, out);
}

pipe_error piped_commands::execute(file_descriptor&& in, file_descriptor& out) const
{
    return execute_impl(in, out);
}


// operator overloads

std::ostream& tep::operator<<(std::ostream& os, const pipe_error& e)
{
    os << e.msg();
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const command& c)
{
    for (size_t ix = 0; ix < c.values().size() - 1; ix++)
        os << c.values()[ix] << " ";
    os << c.values().back();
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const piped_commands& c)
{
    for (const auto& cmd : c.cmds())
        os << cmd << "\n";
    return os;
}


file_descriptor& tep::operator<<(file_descriptor& fd, const char* str)
{
    pipe_error err = fd.write(str, strlen(str));
    if (err)
    {
        std::cerr << err << std::endl;
        throw std::system_error();
    }
    return fd;
}

file_descriptor& tep::operator<<(file_descriptor& fd, const std::string& str)
{
    pipe_error err = fd.write(str.data(), str.size());
    if (err)
    {
        std::cerr << err << std::endl;
        throw std::system_error();
    }
    return fd;
}

file_descriptor& tep::operator<<(file_descriptor& fd, file_descriptor::endl_t)
{
    pipe_error err = fd.write("\n", 1);
    if (err)
    {
        std::cerr << err << std::endl;
        throw std::system_error();
    }
    fd.flush();
    return fd;
}

