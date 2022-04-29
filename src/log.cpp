#include "log.hpp"

#include <array>
#include <cinttypes>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace tep;

#define COLOR_RED "\x1B[31m"
#define COLOR_GREEN "\x1B[32m"
#define COLOR_YELLOW "\x1B[33m"
#define COLOR_BLUE "\x1B[34m"
#define COLOR_CYAN "\x1B[36m"
#define COLOR_RESET "\x1B[0m"

namespace {
using write_func_ptr = void (*)(const log::content &, log::loc);
using stream_getter_ptr = std::ostream &(*)();

constexpr const char error_message[] = "<log error>";

constexpr const std::array<const char *, 5> levels = {
    "debug", "info", "success", "warn", "error"};

constexpr const std::array<const char *, 5> clevels = {
    COLOR_BLUE "debug" COLOR_RESET, COLOR_CYAN "info" COLOR_RESET,
    COLOR_GREEN "success" COLOR_RESET, COLOR_YELLOW "warn" COLOR_RESET,
    COLOR_RED "error" COLOR_RESET};

template <bool colored = false>
constexpr const char *to_string(log::level lvl) {
  if constexpr (colored)
    return clevels[lvl];
  else
    return levels[lvl];
}

constexpr const std::size_t initial_size = 256UL;
std::mutex _logmtx;
std::ofstream _stream;

struct term_endl {};

struct term_newline {};

class timestamp {
  char buff[128];

public:
  timestamp() {
    using namespace std::chrono;
    system_clock::time_point stp(system_clock::now());
    microseconds us = duration_cast<microseconds>(stp.time_since_epoch());
    seconds sec = duration_cast<seconds>(us);
    std::tm tm;
    std::time_t time = sec.count();
    localtime_r(&time, &tm);
    size_t tm_sz = std::strftime(buff, sizeof(buff), "%T", &tm);
    if (!tm_sz) {
      *buff = 0;
      return;
    }
    int written = snprintf(buff + tm_sz, sizeof(buff) - tm_sz, ".%06" PRId64,
                           us.count() % microseconds::period::den);
    if (written < 0 || static_cast<unsigned>(written) >= sizeof(buff) - tm_sz) {
      *buff = 0;
      return;
    }
  }

  explicit operator bool() const { return *buff; }

  friend std::ostream &operator<<(std::ostream &os, const timestamp &ts);
};

std::ostream &operator<<(std::ostream &os, const timestamp &ts) {
  return os << ts.buff;
}

std::ostream &operator<<(std::ostream &os, tep::log::loc at) {
  std::ios::fmtflags flags(os.flags());
  os << at.file << ":" << std::left << std::setw(3) << at.line;
  os.flags(flags);
  return os;
}

std::ostream &operator<<(std::ostream &os, const tep::log::content &cnt) {
  os << cnt.msg;
  return os;
}

std::ostream &operator<<(std::ostream &os, tep::log::level lvl) {
  os << to_string(lvl);
  return os;
}

std::ostream &operator<<(std::ostream &os, term_endl) {
  return os << std::endl;
}

std::ostream &operator<<(std::ostream &os, term_newline) { return os << "\n"; }

template <typename Term>
void write_single(log::level lvl, const log::content &cnt, log::loc at,
                  Term term, std::ostream &os) {
  auto ts = timestamp{};
  if (ts && cnt)
    os << ts << ": " << at << " " << lvl << ": " << cnt << term;
  else
    os << error_message << term;
}

template <typename Term, typename... Args>
void write_multiple(log::level lvl, const log::content &cnt, log::loc at,
                    Term term, Args &...streams) {
  std::ostringstream oss;
  write_single(lvl, cnt, at, term, oss);
  std::string str = oss.str();
  ((streams << str), ...);
}

template <log::level lvl, typename Term, auto &os, auto &...other>
void write_impl(const log::content &cnt, log::loc at) {
  if constexpr (!sizeof...(other))
    write_single(lvl, cnt, at, Term{}, os);
  else
    write_multiple(lvl, cnt, at, Term{}, os, other...);
}

template <auto &os> std::ostream &getter_impl() { return os; }

void do_nothing(const log::content &, log::loc) {}

std::array<std::pair<write_func_ptr, stream_getter_ptr>, 5> funcs = {
    std::pair{do_nothing, getter_impl<_stream>},
    std::pair{do_nothing, getter_impl<_stream>},
    std::pair{do_nothing, getter_impl<_stream>},
    std::pair{do_nothing, getter_impl<_stream>},
    std::pair{do_nothing, getter_impl<_stream>}};

template <auto &os, typename Term, log::level... lvl> void set_funcs() {
  ((funcs[lvl] = {write_impl<lvl, Term, os>, getter_impl<os>}), ...);
}

std::string error_opening_file(const std::string &file) {
  std::string msg("Error opening file ");
  msg.append(file).append(": ").append(std::strerror(errno));
  return msg;
}

static_assert(funcs.size() == log::error + 1);
static_assert(levels.size() == log::error + 1);
static_assert(clevels.size() == log::error + 1);
} // namespace

namespace tep {
log::loc::operator bool() const { return file && line; }

void log::content::init(const char *fmt, ...) {
  msg.resize(initial_size);

  va_list args, args_copy;
  va_start(args, fmt);
  va_copy(args_copy, args);
  int retval = std::vsnprintf(msg.data(), msg.size() + 1, fmt, args);
  va_end(args);

  std::size_t size = static_cast<std::size_t>(retval);
  if (retval < 0)
    msg.resize(0);
  else if (size > msg.size()) {
    msg.resize(size);
    if (std::vsnprintf(msg.data(), msg.size() + 1, fmt, args_copy) < 0)
      msg.resize(0);
  } else if (size < msg.size())
    msg.resize(size);
  va_end(args_copy);
}

log::content::operator bool() const { return !msg.empty(); }

void log::init(bool quiet, const std::string &path) {
  static std::once_flag oflag;
  try {
    std::call_once(
        oflag,
        [](bool quiet, const std::string &path) {
          if (quiet)
            set_funcs<std::cerr, term_endl, error>();
          else if (path.empty()) {
            set_funcs<std::cout, term_newline, debug, info, success>();
            set_funcs<std::cerr, term_newline, warning>();
            set_funcs<std::cerr, term_endl, error>();
          } else {
            _stream.open(path);
            if (!_stream)
              throw std::runtime_error(error_opening_file(path));
            set_funcs<_stream, term_newline, debug, info, success, warning>();
            set_funcs<_stream, term_endl, error>();
          }
        },
        quiet, path);
  } catch (...) {
    throw;
  }
}

std::mutex &log::mutex() { return _logmtx; }

std::ostream &log::stream(level lvl) { return (*funcs[lvl].second)(); }

std::ostream &log::flush(level lvl) { return stream(lvl).flush(); }

void log::write(level lvl, const content &cnt, loc at) {
  std::scoped_lock lock(_logmtx);
  (*funcs[lvl].first)(cnt, at);
}
} // namespace tep
