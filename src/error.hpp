// error.hpp

#pragma once

#include <string>

namespace tep {

// error codes

enum class tracer_errcode {
  SUCCESS,
  SYSTEM_ERROR,
  PTRACE_ERROR,
  READER_ERROR,
  SIGNAL_DURING_SECTION_ERROR,
  NO_SYMBOL,
  NO_TRAP,
  UNSUPPORTED,
  UNKNOWN_ERROR,
};

// error holder

class tracer_error {
public:
  static tracer_error success() { return {tracer_errcode::SUCCESS}; }

private:
  tracer_errcode _code;
  std::string _msg;

public:
  tracer_error(tracer_errcode code);
  tracer_error(tracer_errcode code, const char *msg);
  tracer_error(tracer_errcode code, std::string &&msg);
  tracer_error(tracer_errcode code, const std::string &msg);

  tracer_errcode code() const;
  const std::string &msg() const;

  explicit operator bool() const;
};

// operator overloads

std::ostream &operator<<(std::ostream &os, const tracer_errcode &code);
std::ostream &operator<<(std::ostream &os, const tracer_error &e);

// other

tracer_error get_syserror__(const char *file, int line, int errnum,
                            tracer_errcode code, pid_t tid,
                            const char *comment);

#define get_syserror(errnum, code, tid, comment)                               \
  get_syserror__(__FILE__, __LINE__, errnum, code, tid, comment)

} // namespace tep
