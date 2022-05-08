// ptrace_wrapper.hpp

#pragma once

#include <mutex>
#include <thread>

#include <semaphore.h>
#include <sys/ptrace.h>

namespace tep {

class ptrace_wrapper {
public:
  static ptrace_wrapper instance;

  struct callback_args_t {
    bool randomize;
    char *const *argv;
  };

  using callback_t = void(callback_args_t);

private:
  using ptrace_req = __ptrace_request;

  enum class request : uint32_t;

  struct request_data {
    request req;
    union {
      struct {
        long result;
        ptrace_req req;
        pid_t pid;
        void *addr;
        void *data;
      } ptrace;
      struct {
        pid_t result;
        callback_t *callback;
        callback_args_t args;
      } fork;
    };
    int error;
  };

  request_data _data;
  std::mutex _global_mx;
  std::thread _calling_thread;
  sem_t _req_sem;
  sem_t _res_sem;

  void thread_work() noexcept;

  ptrace_wrapper();
  ~ptrace_wrapper() noexcept;

public:
  long ptrace(int &error, ptrace_req req, pid_t pid, ...) noexcept;
  pid_t fork(int &error, callback_t *callback, callback_args_t args) noexcept;
};

} // namespace tep
